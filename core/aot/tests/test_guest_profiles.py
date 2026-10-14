import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'examples'))
from guest_build import native_flags, prepare_ir_sources


IR = '''target triple = "arm64-apple-macosx14.0.0"
@sink = global i32 0
define internal void @run(i32 %argc) noinline {
entry:
  %single = icmp eq i32 %argc, 1
  br i1 %single, label %one, label %many
one:
  store volatile i32 1, ptr @sink
  br label %done
many:
  store volatile i32 2, ptr @sink
  store volatile i32 3, ptr @sink
  br label %done
done:
  ret void
}
define i32 @main(i32 %argc, ptr %argv) {
  call void @run(i32 %argc)
  ret i32 0
}
'''


class GuestProfileTests(unittest.TestCase):
    def prepare(self, folder):
        source = folder / 'program.ll'
        source.write_text(IR)
        prepare_ir_sources([source], '0' * 64)
        binary = folder / 'instrumented'
        flags = native_flags('-O2', 'none', generate=folder / 'raw')
        subprocess.run(['xcrun', 'clang', *flags, '-Wno-override-module', str(source), '-o', str(binary)], check=True, capture_output=True)
        subprocess.run([str(binary)], check=True, capture_output=True)
        subprocess.run([str(binary), 'second'], check=True, capture_output=True)
        raw = sorted((folder / 'raw').glob('*.profraw'))
        self.assertTrue(raw)
        profile = folder / 'merged.profdata'
        subprocess.run(['xcrun', 'llvm-profdata', 'merge', *map(str, raw), '-o', str(profile)], check=True, capture_output=True)
        return source, profile

    def test_ir_profile_roundtrip_with_native_options(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source, profile = self.prepare(folder)
            moved = folder / 'different-output' / source.name
            moved.parent.mkdir()
            moved.write_text(IR)
            prepare_ir_sources([moved], '0' * 64)
            self.assertEqual(source.read_bytes(), moved.read_bytes())
            source = moved
            binary = folder / 'profiled'
            flags = native_flags('-O2', 'thin', use=profile)
            built = subprocess.run(['xcrun', 'clang', *flags, '-Wno-override-module', str(source), '-o', str(binary)], capture_output=True)
            self.assertEqual((built.returncode, built.stderr), (0, b''))
            output_ir = folder / 'profiled.ll'
            subprocess.run(['xcrun', 'clang', *flags, '-Wno-override-module', '-S', '-emit-llvm', str(source), '-o', str(output_ir)], check=True, capture_output=True)
            text = output_ir.read_text()
            function = re.search(r'define internal [^\n]*@run\([^\n]*!prof !(\d+)', text)
            self.assertIsNotNone(function)
            self.assertIn(f'!{function[1]} = !{{!"function_entry_count", i64 2}}', text)
            profile.unlink()
            self.assertEqual(subprocess.run([str(binary)], capture_output=True).returncode, 0)
            self.assertEqual(subprocess.run([str(binary), 'second'], capture_output=True).returncode, 0)

    def test_changed_control_flow_rejects_the_profile(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source, profile = self.prepare(folder)
            source.write_text(IR.replace('  %single = icmp eq i32 %argc, 1', '''  %loaded = load volatile i32, ptr @sink
  %extra = icmp eq i32 %loaded, 99
  br i1 %extra, label %unexpected, label %next
unexpected:
  store volatile i32 4, ptr @sink
  ret void
next:
  %single = icmp eq i32 %argc, 1'''))
            prepare_ir_sources([source], '0' * 64)
            flags = native_flags('-O2', 'none', use=profile)
            built = subprocess.run(['xcrun', 'clang', *flags, '-Wno-override-module', str(source), '-o', str(folder / 'rejected')], capture_output=True)
            self.assertNotEqual(built.returncode, 0)
            self.assertIn(b'hash mismatch', built.stderr)


if __name__ == '__main__':
    unittest.main()
