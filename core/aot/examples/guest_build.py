import argparse
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def native_flags(optimization, lto, cpu=None, generate=None, use=None):
    if generate is not None and use is not None:
        raise ValueError('profile generation and use are mutually exclusive')
    flags = [optimization, '-arch', 'arm64']
    if lto != 'none':
        flags.append('-flto=' + lto)
    if cpu:
        flags.append('-mcpu=' + cpu)
    if generate is not None:
        flags += [f'-fprofile-generate={generate}', '-fprofile-update=atomic']
    if use is not None:
        flags += [f'-fprofile-use={use}', '-Werror=profile-instr-out-of-date', '-Werror=backend-plugin']
    return flags


def prepare_ir_sources(modules, input_hash):
    for module in modules:
        module.write_text(f'source_filename = "guest/{input_hash}/{module.name}"\n' + module.read_text())
    return {module.name: digest(module) for module in modules}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--name', required=True)
    parser.add_argument('--optimization', choices=('-O1', '-O2', '-O3'), default='-O2')
    parser.add_argument('--profile', choices=('single', 'ordered', 'epochs'), default='ordered')
    parser.add_argument('--optimize-calls', action='store_true')
    parser.add_argument('--seal-image', action='store_true')
    parser.add_argument('--thin-lto', action='store_true')
    parser.add_argument('--lto', choices=('none', 'thin', 'full'))
    parser.add_argument('--cpu')
    profiles = parser.add_mutually_exclusive_group()
    profiles.add_argument('--pgo-generate', type=Path)
    profiles.add_argument('--pgo-use', type=Path)
    args = parser.parse_args()
    if args.thin_lto and args.lto not in (None, 'thin'):
        parser.error('--thin-lto conflicts with the selected --lto mode')
    if args.pgo_use is not None and not args.pgo_use.is_file():
        parser.error('--pgo-use requires an existing llvm-profdata profile')
    worktree = Path(__file__).resolve().parents[3]
    sys.path[:0] = [str(worktree / 'core/aot'), str(worktree / '3rdparty/pyelftools'), str(worktree / '3rdparty/capstone/bindings/python')]
    import guest_emit
    started = time.monotonic()
    output = args.output.resolve()
    if output.exists():
        parser.error('choose a fresh output folder')
    if Path(args.name).name != args.name or args.name in ('', '.', '..'):
        parser.error('name must be one file name')
    lto_mode = args.lto or ('thin' if args.thin_lto else 'none')
    lto = ['-flto=' + lto_mode] if lto_mode != 'none' else []
    profile = args.pgo_generate or args.pgo_use
    profile = profile.resolve() if profile is not None else None
    profile_hash = digest(profile) if args.pgo_use is not None else None
    flags = native_flags(args.optimization, lto_mode, args.cpu,
                         profile if args.pgo_generate is not None else None,
                         profile if args.pgo_use is not None else None)
    native = worktree / 'core/libs/native'
    runtime = sorted(native.glob('NativeAotGuest*.cpp')) + sorted(native.glob('NativeAotGlibc*.cpp')) + [worktree / 'core/libs/prx/libkernel/Pthread/src/GuestTlsMacOs.cpp']
    runtime_inputs = runtime + sorted(native.glob('NativeAotGuest*.hpp')) + sorted(native.glob('NativeAotGlibc*.hpp')) + [worktree / 'core/libs/prx/libkernel/Pthread/include/GuestTlsMacOs.hpp']
    compiler_inputs = sorted((worktree / 'core/aot').glob('*.py')) + sorted((worktree / 'core/aot/catalogs').glob('*.json'))
    compiler_hashes = {str(path.relative_to(worktree)): digest(path) for path in compiler_inputs}
    runtime_hashes = {str(path.relative_to(worktree)): digest(path) for path in runtime_inputs}
    builder_hash = digest(__file__)
    input_hash = digest(args.input)
    head = subprocess.run(['git', '-C', str(worktree), 'rev-parse', 'HEAD'], capture_output=True, text=True, check=True).stdout.strip()
    ir = output / 'ir'
    report = guest_emit.build(args.input, ir, concurrent=args.profile != 'single', memory_epochs=args.profile == 'epochs', optimize_calls=args.optimize_calls, seal_image=args.seal_image)
    lowered = time.monotonic()
    modules = sorted(ir.glob('*.ll'))
    ir_hashes = prepare_ir_sources(modules, input_hash)
    from concurrent.futures import ThreadPoolExecutor
    def compile_ir(module):
        command = ['xcrun', 'clang', '-c', *flags, '-Wno-override-module', str(module), '-o', str(module) + '.o']
        subprocess.run(command, check=True)
        return command
    with ThreadPoolExecutor(max_workers=3) as pool:
        commands = list(pool.map(compile_ir, modules))
    compiled = time.monotonic()
    binary = output / args.name
    command = ['xcrun', 'clang++', '-std=c++20', *flags, f'-I{worktree / "core/libs"}', *map(str, runtime),
               *[f'{module}.o' for module in modules], '-o', str(binary)]
    subprocess.run(command, check=True)
    commands.append(command)
    linked = time.monotonic()
    if (compiler_hashes != {str(path.relative_to(worktree)): digest(path) for path in compiler_inputs}
            or runtime_hashes != {str(path.relative_to(worktree)): digest(path) for path in runtime_inputs}
            or builder_hash != digest(__file__) or input_hash != digest(args.input)
            or ir_hashes != {module.name: digest(module) for module in modules}
            or (args.pgo_use is not None and profile_hash != digest(profile))):
        raise ValueError('build inputs changed during compilation; the output is not qualified')
    current_head = subprocess.run(['git', '-C', str(worktree), 'rev-parse', 'HEAD'], capture_output=True, text=True, check=True).stdout.strip()
    if current_head != head:
        raise ValueError('compiler revision changed during compilation; the output is not qualified')
    dirty = subprocess.run(['git', '-C', str(worktree), 'status', '--porcelain', '--', 'core'], capture_output=True, text=True, check=True).stdout
    clang = subprocess.run(['xcrun', 'clang', '--version'], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    receipt = {'schema': 'anyps5.transfer.conversion-receipt.v1', 'input': str(args.input.resolve()), 'input_sha256': input_hash,
               'compiler_revision': head, 'compiler_dirty': bool(dirty), 'native_compiler': clang, 'optimization': args.optimization + ' '.join([''] + lto), 'profile': args.profile, 'optimized_calls': args.optimize_calls or args.profile == 'epochs', 'sealed_image': args.seal_image,
               'native_cpu': args.cpu, 'lto': lto_mode, 'commands': commands,
               'ir_sources': ir_hashes,
               'pgo': {'mode': 'generate' if args.pgo_generate is not None else 'use' if args.pgo_use is not None else 'none',
                       'path': str(profile) if profile is not None else None, 'sha256': profile_hash,
                       'training_binary': args.pgo_generate is not None},
               'output': str(binary), 'output_sha256': digest(binary),
               'compiler_sources': compiler_hashes,
               'builder_sha256': builder_hash,
               'runtime_sources': runtime_hashes,
               'lowering': {key: report[key] for key in ('units', 'instructions', 'jump_tables', 'imports', 'call_effects', 'native_leaf_values', 'native_leaf_candidates')},
               'obligations': report['obligations'], 'unsupported': report['unsupported'],
               'seconds': {'lowering': round(lowered - started, 3), 'native_compile': round(compiled - lowered, 3),
                           'link': round(linked - compiled, 3)},
               'translation_in_distributed_program': 'none: the executable contains only AOT-compiled ARM64 code, the native runtime and providers'}
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({key: receipt[key] for key in ('output_sha256', 'lowering', 'seconds')}, indent=1))


if __name__ == '__main__':
    main()
