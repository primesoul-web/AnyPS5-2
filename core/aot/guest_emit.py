import argparse
import json
import subprocess
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

from capstone.x86 import X86_OP_IMM, X86_OP_MEM

import guest_lift
from guest_ops import (FLAGS, GPR, STATE_XMM, TRAP_FALLTHROUGH, TRAP_HALT, TRAP_INVALID, TRAP_UNKNOWN_ENTRY,
                       Unsupported, Vector)

TRAP_UNKNOWN_TARGET = 1
TRIPLE = 'arm64-apple-macosx14.0.0'
RUNTIME = ['declare void @NativeAotGuestTrap(i64, i64, i64) noreturn', 'declare ptr @NativeAotGuestLookup(i64)', 'declare ptr @NativeAotGuestFind(i64)',
           'declare void @NativeAotGuestCpuid(ptr)', 'declare i64 @NativeAotGuestReadTsc()']


def symbol(address):
    return f'@__aps5_g_{address:x}'


class Lowering(Vector):
    def load(self, address, bits):
        if not self.concurrent:
            return super().load(address, bits)
        if bits > 64:
            low = self.load(address, 64)
            high = self.load(self.v(f'add i64 {address}, 8'), bits - 64)
            wide = self.v(f'zext i64 {low} to i{bits}')
            shifted = self.v(f'shl i{bits} {self.v(f"zext i{bits - 64} {high} to i{bits}")}, 64')
            return self.v(f'or i{bits} {wide}, {shifted}')
        opcode = {8: 'ldrb', 16: 'ldrh', 32: 'ldr', 64: 'ldr'}[bits]
        width = 'x' if bits == 64 else 'w'
        value = self.v(f'call i64 asm sideeffect "dmb ish\\0A{opcode} ${{0:{width}}}, [$1]\\0Admb ish", "=r,r,~{{memory}}"(i64 {address})')
        return value if bits == 64 else self.v(f'trunc i64 {value} to i{bits}')

    def store(self, address, value, bits):
        if not self.concurrent:
            return super().store(address, value, bits)
        if bits > 64:
            self.store(address, self.v(f'trunc i{bits} {value} to i64'), 64)
            high = self.v(f'lshr i{bits} {value}, 64')
            self.store(self.v(f'add i64 {address}, 8'), self.v(f'trunc i{bits} {high} to i{bits - 64}'), bits - 64)
            return
        opcode = {8: 'strb', 16: 'strh', 32: 'str', 64: 'str'}[bits]
        width = 'x' if bits == 64 else 'w'
        extended = value if bits == 64 else self.v(f'zext i{bits} {value} to i64')
        self.emit(f'call void asm sideeffect "dmb ish\\0A{opcode} ${{0:{width}}}, [$1]\\0Admb ish", "r,r,~{{memory}}"(i64 {extended}, i64 {address})')

    def flush(self):
        for index in range(16):
            pointer = self.v(f'getelementptr i8, ptr %s, i64 {8 * index}')
            self.emit(f'store i64 {self.v(f"load i64, ptr %R{index}")}, ptr {pointer}')
        for index in range(16):
            pointer = self.v(f'getelementptr i8, ptr %s, i64 {STATE_XMM + 16 * index}')
            self.emit(f'store i128 {self.v(f"load i128, ptr %X{index}")}, ptr {pointer}, align 16')

    def reload(self):
        for index in range(16):
            pointer = self.v(f'getelementptr i8, ptr %s, i64 {8 * index}')
            self.emit(f'store i64 {self.v(f"load i64, ptr {pointer}")}, ptr %R{index}')
        for index in range(16):
            pointer = self.v(f'getelementptr i8, ptr %s, i64 {STATE_XMM + 16 * index}')
            self.emit(f'store i128 {self.v(f"load i128, ptr {pointer}, align 16")}, ptr %X{index}')

    def prologue(self):
        self.lines.append(f'define hidden i64 {symbol(self.unit.start)}(ptr noalias %s, i64 %entry) {{')
        self.lines.append('prologue:')
        for index in range(16):
            self.emit(f'%R{index} = alloca i64')
        for index in range(16):
            self.emit(f'%X{index} = alloca i128, align 16')
        for flag in FLAGS:
            self.emit(f'%F{flag} = alloca i1')
            self.emit(f'store i1 false, ptr %F{flag}')
        self.emit('%Resume = alloca i64')
        self.reload()
        self.emit('%base = load i64, ptr @__aps5_guest_base')
        expected = self.v(f'add i64 %base, {self.unit.start}')
        bad = self.label()
        first = f'L{self.unit.start:x}.trace' if self.trace else f'L{self.unit.start:x}'
        self.emit(f'br i1 {self.v(f"icmp eq i64 %entry, {expected}")}, label %{first}, label %{bad}')
        self.place(bad)
        self.emit(f'call void @NativeAotGuestTrap(i64 {TRAP_UNKNOWN_ENTRY}, i64 %entry, i64 {expected})')
        self.emit('unreachable')
        if self.trace:
            self.lines.append(f'L{self.unit.start:x}.trace:')

    def absolute(self, address):
        return self.v(f'add i64 %base, {address}')

    def target_value(self):
        operand = self.ins.operands[0]
        if operand.type == X86_OP_IMM:
            return self.absolute(operand.imm)
        return self.read(operand, 64)

    def invoke(self, function, target):
        self.flush()
        return self.v(f'call i64 {function}(ptr %s, i64 {target})')

    def leave_with(self, value):
        if self.trace:
            self.emit(f'call void @NativeAotGuestTraceLeave(ptr %s, i64 {self.unit.start}, i64 {value})')
        self.emit(f'ret i64 {value}')

    def direct(self, address):
        return symbol(address) if address in self.units else None

    def call_target(self, target_address, target):
        function = self.direct(target_address) if target_address is not None else None
        if function is None:
            function = self.v(f'call ptr @NativeAotGuestLookup(i64 {target})')
        return function

    def control(self, ins):
        unit, mnemonic = self.unit, ins.mnemonic
        following = ins.address + ins.size
        if mnemonic == 'call':
            operand = ins.operands[0]
            target = self.target_value()
            function = self.call_target(operand.imm if operand.type == X86_OP_IMM else None, target)
            back = self.absolute(following)
            rsp = self.v(f'sub i64 {self.get_gpr("rsp")}, 8')
            self.store(rsp, back, 64)
            self.set_gpr('rsp', rsp)
            result = self.invoke(function, target)
            self.reload()
            propagate = self.label()
            continuation = f'L{following:x}' if following in unit.instructions else self.label()
            self.emit(f'br i1 {self.v(f"icmp eq i64 {result}, {back}")}, label %{continuation}, label %{propagate}')
            self.place(propagate)
            if self.resume_points():
                self.emit(f'store i64 {result}, ptr %Resume')
                self.emit('br label %resume')
                self.needs_resume = True
            else:
                self.leave_with(result)
            if following not in unit.instructions:
                self.place(continuation)
                self.trap(TRAP_FALLTHROUGH)
            return
        if mnemonic == 'ret':
            rsp = self.get_gpr('rsp')
            back = self.load(rsp, 64)
            extra = ins.operands[0].imm if ins.operands else 0
            self.set_gpr('rsp', self.v(f'add i64 {rsp}, {8 + extra}'))
            self.flush()
            self.leave_with(back)
            return
        if mnemonic in ('hlt', 'ud2', 'int3'):
            self.trap({'hlt': TRAP_HALT, 'ud2': TRAP_INVALID, 'int3': TRAP_INVALID}[mnemonic])
            return
        if mnemonic == 'jmp':
            if ins.address in unit.tables:
                self.table(ins)
                return
            operand = ins.operands[0]
            if operand.type == X86_OP_IMM and ins.address not in unit.tails:
                self.emit(f'br label %L{operand.imm:x}')
                return
            target = self.target_value()
            if operand.type == X86_OP_IMM:
                self.leave_with(self.invoke(self.call_target(operand.imm, target), target))
                return
            function = self.v(f'call ptr @NativeAotGuestFind(i64 {target})')
            local, remote = self.label(), self.label()
            self.emit(f'br i1 {self.v(f"icmp eq ptr {function}, null")}, label %{local}, label %{remote}')
            self.place(local)
            self.flush()
            self.leave_with(target)
            self.place(remote)
            self.leave_with(self.invoke(function, target))
            return
        suffix = mnemonic[1:]
        target = ins.operands[0].imm
        condition = self.condition(suffix)
        taken = f'L{target:x}'
        if ins.address in unit.tails:
            taken = self.label()
        self.emit(f'br i1 {condition}, label %{taken}, label %L{following:x}')
        if ins.address in unit.tails:
            self.place(taken)
            absolute = self.absolute(target)
            self.leave_with(self.invoke(symbol(target), absolute))

    def resume_points(self):
        if not hasattr(self, '_resume'):
            unit = self.unit
            points = set(getattr(unit, 'landing_pads', []))
            for address, ins in unit.instructions.items():
                following = address + ins.size
                if ins.mnemonic == 'call' and following in unit.instructions:
                    points.add(following)
            self._resume = sorted(points)
        return self._resume

    def table(self, ins):
        description = self.unit.tables[ins.address]
        value = self.read(ins.operands[0], 64)
        offset = self.v(f'sub i64 {value}, %base')
        default = self.label()
        cases = ' '.join(f'i64 {target}, label %L{target:x}' for target in sorted(set(description['targets'])))
        self.emit(f'switch i64 {offset}, label %{default} [ {cases} ]')
        self.place(default)
        self.emit(f'call void @NativeAotGuestTrap(i64 {TRAP_UNKNOWN_TARGET}, i64 {value}, i64 {self.absolute(ins.address)})')
        self.emit('unreachable')

    def instruction(self, ins):
        self.ins = ins
        mnemonic = ins.mnemonic
        if mnemonic in ('call', 'ret', 'jmp', 'hlt', 'ud2', 'int3') or (mnemonic.startswith('j') and mnemonic[1:]):
            self.control(ins)
            return
        if 0xf0 in ins.prefix:
            self.locked()
        elif mnemonic.startswith('rep') or mnemonic in ('movsb', 'movsw', 'movsq', 'stosb', 'stosw', 'stosd', 'stosq'):
            self.string()
        elif mnemonic.startswith('cmov'):
            self.cmov(mnemonic[4:])
        elif mnemonic.startswith('set'):
            self.setcc(mnemonic[3:])
        else:
            handler = getattr(self, 'op_' + mnemonic, None)
            if handler is None:
                self.unsupported('instruction semantics are not implemented')
            handler()
        following = ins.address + ins.size
        if following in self.unit.instructions:
            self.emit(f'br label %L{following:x}')
        else:
            self.trap(TRAP_FALLTHROUGH)


def lower_unit(unit, units, trace=False, concurrent=False):
    lowering = Lowering(unit, symbol(unit.start))
    lowering.units = units
    lowering.trace = trace
    lowering.concurrent = concurrent
    lowering.prologue()
    if trace:
        lowering.emit(f'call void @NativeAotGuestTraceEnter(ptr %s, i64 {unit.start})')
        lowering.emit(f'br label %L{unit.start:x}')
    errors = []
    for address in sorted(unit.instructions):
        lowering.lines.append(f'L{address:x}:')
        mark = len(lowering.lines)
        try:
            lowering.instruction(unit.instructions[address])
        except Unsupported as error:
            errors.append(str(error))
            del lowering.lines[mark:]
            lowering.ins = unit.instructions[address]
            lowering.trap(10)
    if getattr(lowering, 'needs_resume', False):
        lowering.lines.append('resume:')
        result = lowering.v('load i64, ptr %Resume')
        outside = lowering.label()
        cases = ' '.join(f'i64 {address}, label %L{address:x}' for address in lowering.resume_points())
        lowering.emit(f'switch i64 {lowering.v(f"sub i64 {result}, %base")}, label %{outside} [ {cases} ]')
        lowering.place(outside)
        lowering.leave_with(result)
    lowering.lines.append('}')
    return '\n'.join(lowering.lines), lowering.declarations, errors


def describe_image(program, units):
    view = program.view
    loads = view.loads
    blob, segments = bytearray(), []
    for segment in loads:
        segments.append((segment['vaddr'], segment['filesz'], segment['memsz'], len(blob), segment['flags']))
        blob += view.raw[segment['offset']:segment['offset'] + segment['filesz']]
    symbols, flags, index = [], [], {}
    relocations = []
    for relocation in program.dynamic['relocations']:
        number = 0
        if relocation['kind'] in (1, 6, 7):
            entry = program.dynamic['symbols'][relocation['symbol_index']]
            name = entry['name']
            if name not in index:
                index[name] = len(symbols)
                symbols.append(name)
                flags.append(1 if entry['info'] >> 4 == 2 else 0)
            number = index[name]
        relocations.append((relocation['slot'], relocation['kind'], number, relocation['addend']))
    span = max(s['vaddr'] + s['memsz'] for s in loads)
    relro_address, relro_size = program.relro
    exports = sorted((symbol['name'], symbol['value']) for symbol in program.dynamic['symbols'][1:]
                     if symbol['name'] and symbol['section'] and symbol['section'] < 0xff00 and symbol['info'] & 15 in (1, 2))
    tls = program.tls
    tls_fields = (tls['vaddr'], tls['filesz'], tls['memsz'], max(tls['align'], 1)) if tls else (0, 0, 0, 1)
    return {'blob': bytes(blob), 'segments': segments, 'symbols': symbols, 'flags': flags, 'relocations': relocations,
            'span': span, 'relro': (relro_address, relro_size), 'entry': view.entry,
            'entries': sorted(units), 'tls': tls_fields, 'exports': exports}


def string_constant(name, text):
    data = text.encode() + b'\0'
    escaped = ''.join(chr(b) if 32 <= b < 127 and b not in (34, 92) else f'\\{b:02X}' for b in data)
    return f'{name} = private unnamed_addr constant [{len(data)} x i8] c"{escaped}"'


def descriptor_module(image, concurrent=False):
    lines = [f'target triple = "{TRIPLE}"', '@__aps5_guest_base = hidden global i64 0']
    for address in image['entries']:
        lines.append(f'declare i64 {symbol(address)}(ptr, i64)')
    blob = image['blob']
    escaped = ''.join(f'\\{b:02X}' for b in blob)
    lines.append(f'@__aps5_guest_bytes = private constant [{len(blob)} x i8] c"{escaped}", align 16')
    lines.append(f'@__aps5_guest_segments = private constant [{len(image["segments"])} x [5 x i64]] [' +
                 ', '.join('[5 x i64] [' + ', '.join(f'i64 {v}' for v in s) + ']' for s in image['segments']) + ']')
    relocations = image['relocations'] or [(0, 0, 0, 0)]
    lines.append(f'@__aps5_guest_relocations = private constant [{len(relocations)} x [4 x i64]] [' +
                 ', '.join('[4 x i64] [' + ', '.join(f'i64 {v}' for v in r) + ']' for r in relocations) + ']')
    names = []
    for number, name in enumerate(image['symbols']):
        lines.append(string_constant(f'@__aps5_guest_symbol_{number}', name))
        names.append(f'ptr @__aps5_guest_symbol_{number}')
    symbol_count = len(image['symbols'])
    lines.append(f'@__aps5_guest_symbols = private constant [{max(symbol_count, 1)} x ptr] [{", ".join(names) or "ptr null"}]')
    lines.append(f'@__aps5_guest_symbol_flags = private constant [{max(symbol_count, 1)} x i64] [{", ".join(f"i64 {f}" for f in image["flags"]) or "i64 0"}]')
    lines.append(f'@__aps5_guest_entries = private constant [{len(image["entries"])} x {{ i64, ptr }}] [' +
                 ', '.join(f'{{ i64, ptr }} {{ i64 {a}, ptr {symbol(a)} }}' for a in image['entries']) + ']')
    exports = image['exports']
    for number, (name, _) in enumerate(exports):
        lines.append(string_constant(f'@__aps5_guest_export_{number}', name))
    lines.append(f'@__aps5_guest_exports = private constant [{max(len(exports), 1)} x {{ ptr, i64 }}] [' +
                 (', '.join(f'{{ ptr, i64 }} {{ ptr @__aps5_guest_export_{number}, i64 {address} }}' for number, (_, address) in enumerate(exports)) or '{ ptr, i64 } zeroinitializer') + ']')
    fields = ['i64 2', 'ptr @__aps5_guest_bytes', f'i64 {len(blob)}', 'ptr @__aps5_guest_segments', f'i64 {len(image["segments"])}',
              'ptr @__aps5_guest_relocations', f'i64 {len(image["relocations"])}', 'ptr @__aps5_guest_symbols',
              'ptr @__aps5_guest_symbol_flags', f'i64 {symbol_count}', 'ptr @__aps5_guest_entries', f'i64 {len(image["entries"])}',
              f'i64 {image["entry"]}', f'i64 {image["span"]}', f'i64 {image["relro"][0]}', f'i64 {image["relro"][1]}',
              'ptr @__aps5_guest_base', *[f'i64 {value}' for value in image['tls']], 'ptr @__aps5_guest_exports', f'i64 {len(exports)}', f'i64 {int(concurrent)}']
    types = ', '.join(field.split(' ')[0] for field in fields)
    lines.append(f'@__aps5_guest_image = constant {{ {types} }} {{ {", ".join(fields)} }}')
    return '\n'.join(lines) + '\n'


def shard_module(texts, declarations, defined, called):
    lines = [f'target triple = "{TRIPLE}"', '@__aps5_guest_base = external hidden global i64', *RUNTIME, *sorted(declarations)]
    for address in sorted(called - defined):
        lines.append(f'declare hidden i64 {symbol(address)}(ptr, i64)')
    lines.extend(texts)
    return '\n\n'.join(lines) + '\n'


def callees(unit, units):
    return {target for target in [*unit.calls.values(), *unit.tails.values()] if target in units}


def lower_shard(arguments):
    raw, starts, output, extra, trace, concurrent = arguments
    program = guest_lift.Program(raw)
    units, _, _ = guest_lift.discover(program, extra)
    texts, declarations, errors, called = [], set(), [], set()
    for start in starts:
        text, declared, failures = lower_unit(units[start], units, trace, concurrent)
        texts.append(text)
        declarations |= declared
        errors.extend(failures)
        called |= callees(units[start], units)
    if trace:
        declarations |= {'declare void @NativeAotGuestTraceEnter(ptr, i64)', 'declare void @NativeAotGuestTraceLeave(ptr, i64, i64)'}
    Path(output).write_text(shard_module(texts, declarations, set(starts), called))
    return errors


def build(input_path, output_dir, shards=16, jobs=8, extra=(), strict=True, trace=False, concurrent=False):
    started = time.monotonic()
    raw = Path(input_path).read_bytes()
    program = guest_lift.Program(raw)
    units, failures, obligations = guest_lift.discover(program, extra)
    output = Path(output_dir)
    output.mkdir(parents=True, exist_ok=True)
    image = describe_image(program, units)
    (output / 'image.ll').write_text(descriptor_module(image, concurrent))
    ordered = sorted(units, key=lambda start: -len(units[start].instructions))
    buckets = [[] for _ in range(shards)]
    sizes = [0] * shards
    for start in ordered:
        smallest = sizes.index(min(sizes))
        buckets[smallest].append(start)
        sizes[smallest] += len(units[start].instructions)
    work = [(raw, sorted(bucket), str(output / f'shard{number:02}.ll'), tuple(extra), trace, concurrent) for number, bucket in enumerate(buckets) if bucket]
    if jobs == 1:
        errors = [error for item in work for error in lower_shard(item)]
    else:
        with ProcessPoolExecutor(jobs) as pool:
            errors = [error for result in pool.map(lower_shard, work) for error in result]
    report = {'schema': 'anyps5.aot.guest-lowering.v1', 'input_sha256': program.sha256, 'units': len(units),
              'memory_profile': 'ordered-normal-memory' if concurrent else 'single-thread',
              'instructions': sum(len(unit.instructions) for unit in units.values()),
              'jump_tables': {status: sum(1 for unit in units.values() for table in unit.tables.values() if table['status'] == status)
                              for status in ('qualified', 'candidate')},
              'unsupported': sorted(set(errors)), 'failed_soft_roots': failures, 'obligations': obligations,
              'imports': image['symbols'], 'seconds': round(time.monotonic() - started, 3)}
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    if strict and errors:
        raise guest_lift.LiftError(f'{len(set(errors))} unsupported instruction sites, first: {sorted(set(errors))[0]}')
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--shards', type=int, default=16)
    parser.add_argument('--inventory', action='store_true')
    parser.add_argument('--trace', action='store_true')
    parser.add_argument('--concurrent', action='store_true')
    args = parser.parse_args()
    report = build(args.input, args.output, args.shards, strict=not args.inventory, trace=args.trace, concurrent=args.concurrent)
    print(json.dumps({key: report[key] for key in ('units', 'instructions', 'jump_tables', 'seconds')}))
    print(f'{len(report["unsupported"])} unsupported instruction sites')


if __name__ == '__main__':
    main()
