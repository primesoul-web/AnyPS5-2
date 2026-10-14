import json
import subprocess
import tempfile
import unittest
from pathlib import Path

import guest_lab
import guest_lift

CASES = json.loads((Path(__file__).parent / 'guest_lab_cases.json').read_text())
CHUNK = 110


class GuestSemanticsTests(unittest.TestCase):
    def compare(self, cases, samples=48):
        with guest_lab.lab_directory() as directory:
            failures = guest_lab.Lab(directory).compare(cases, samples=samples, seed=11)
        self.assertEqual(failures[:3], [])

    def test_forms_0(self):
        self.compare(CASES[0 * CHUNK:1 * CHUNK])

    def test_forms_1(self):
        self.compare(CASES[1 * CHUNK:2 * CHUNK])

    def test_forms_2(self):
        self.compare(CASES[2 * CHUNK:3 * CHUNK])

    def test_forms_3(self):
        self.compare(CASES[3 * CHUNK:4 * CHUNK])

    def test_forms_4(self):
        self.compare(CASES[4 * CHUNK:5 * CHUNK])

    def test_forms_5(self):
        self.compare(CASES[5 * CHUNK:])

    def test_cases_cover_every_chunk(self):
        self.assertLessEqual(len(CASES), 6 * CHUNK + CHUNK)


PRELUDE = '''
.globl _start
_start:
    and rsp, -16
'''

EXIT = '''
    call qword ptr [rip + exit_slot]
'''


class GuestProgramTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='anyps5-guest-program-')
        self.directory = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def program(self, source, imports=('exit',), data_imports=(), relative=(), strict=True, unwind=()):
        slots = ''.join(f'.p2align 3\n{name}_slot: .quad 0\n' for name in [*imports, *data_imports])
        code, labels = guest_lab.assemble_program(source + slots, self.directory)
        raw = guest_lab.program_image(code, labels, [(name, f'{name}_slot') for name in imports],
                                      [(name, f'{name}_slot') for name in data_imports], relative=relative, unwind=unwind)
        return guest_lab.build_program(raw, self.directory, strict=strict)

    def run_binary(self, binary):
        return subprocess.run([str(binary)], capture_output=True, timeout=20)

    def test_variadic_provider_reads_registers_and_stack(self):
        binary, _ = self.program(PRELUDE + '''
    mov rdi, qword ptr [rip + stdout_slot]
    mov rdi, qword ptr [rdi]
    lea rsi, [rip + format]
    mov edx, 1
    mov ecx, 2
    mov r8d, 3
    mov r9d, -4
    movsd xmm0, qword ptr [rip + first]
    movsd xmm1, qword ptr [rip + second]
    lea rax, [rip + text]
    push rax
    push 0
    push 6
    push 5
    mov eax, 2
    call qword ptr [rip + fprintf_slot]
    add rsp, 32
    mov edi, 3
''' + EXIT + '''
format: .asciz "%d %u %x %d %ld %5ld %.2f %.3e %p %-6s|\\n"
text: .asciz "hello"
.p2align 3
first: .double 1.5
second: .double 2.25
''', imports=('fprintf', 'exit'), data_imports=('stdout',))
        result = self.run_binary(binary)
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertEqual(result.stdout, b'1 2 3 -4 5     6 1.50 2.250e+00 (nil) hello |\n')

    def test_guest_callback_through_provider(self):
        binary, _ = self.program(PRELUDE + '''
    sub rsp, 64
    mov qword ptr [rsp], 9
    mov qword ptr [rsp + 8], -3
    mov qword ptr [rsp + 16], 7
    mov qword ptr [rsp + 24], 0
    mov qword ptr [rsp + 32], 100
    mov qword ptr [rsp + 40], -50
    mov qword ptr [rsp + 48], 7
    mov qword ptr [rsp + 56], 1
    mov rdi, rsp
    mov esi, 8
    mov edx, 8
    lea rcx, [rip + compare]
    call qword ptr [rip + qsort_slot]
    mov rdi, rsp
    mov esi, 8
    mov edx, 8
    mov rcx, qword ptr [rip + stdout_slot]
    mov rcx, qword ptr [rcx]
    call qword ptr [rip + fwrite_slot]
    xor edi, edi
''' + EXIT + '''
compare:
    mov rax, qword ptr [rdi]
    mov rcx, qword ptr [rsi]
    xor edx, edx
    cmp rax, rcx
    setg dl
    mov eax, -1
    cmovge eax, edx
    ret
''', imports=('qsort', 'fwrite', 'exit'), data_imports=('stdout',))
        result = self.run_binary(binary)
        self.assertEqual(result.returncode, 0, result.stderr)
        values = [int.from_bytes(result.stdout[i:i + 8], 'little', signed=True) for i in range(0, 64, 8)]
        self.assertEqual(values, sorted([9, -3, 7, 0, 100, -50, 7, 1]))

    def test_unknown_indirect_target_traps(self):
        binary, _ = self.program(PRELUDE + '''
    mov rax, rsp
    call rax
    xor edi, edi
''' + EXIT)
        result = self.run_binary(binary)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'control transfer to an address without converted code', result.stderr)

    def test_mismatched_return_address_is_not_followed(self):
        binary, _ = self.program(PRELUDE + '''
    call redirect
    mov edi, 1
''' + EXIT + '''
landing:
    mov edi, 7
''' + EXIT + '''
redirect:
    lea rax, [rip + landing]
    mov qword ptr [rsp], rax
    ret
''')
        result = self.run_binary(binary)
        self.assertNotEqual(result.returncode, 7)
        self.assertIn(b'guest return address does not match any converted caller', result.stderr)

    def test_missing_import_fails_at_startup(self):
        binary, _ = self.program(PRELUDE + '''
    call qword ptr [rip + definitely_missing_slot]
    xor edi, edi
''' + EXIT, imports=('definitely_missing', 'exit'))
        result = self.run_binary(binary)
        self.assertEqual(result.returncode, 127)
        self.assertIn(b'missing import providers: definitely_missing', result.stderr)

    def test_bounded_and_scanned_jump_tables(self):
        source = PRELUDE + '''
    mov eax, dword ptr [rsp]
    cmp eax, 3
    ja done
    lea rcx, [rip + table]
    movsxd rax, dword ptr [rcx + 4*rax]
    add rax, rcx
    jmp rax
case0:
    mov edi, 10
    jmp done_exit
case1:
    mov edi, 11
    jmp done_exit
case2:
    mov edi, 12
    jmp done_exit
case3:
    mov edi, 13
    jmp done_exit
done:
    mov edi, 99
done_exit:
''' + EXIT + '''
.p2align 2
table:
    .long case0 - table
    .long case1 - table
    .long case2 - table
    .long case3 - table
'''
        binary, report = self.program(source)
        self.assertEqual(report['jump_tables'], {'qualified': 1, 'candidate': 0})
        result = self.run_binary(binary)
        self.assertEqual(result.returncode, 11, result.stderr)

    def test_scanned_table_keeps_runtime_membership(self):
        source = PRELUDE + '''
    mov eax, dword ptr [rsp]
    add eax, 1
    and eax, 3
    lea rcx, [rip + table]
    movsxd rax, dword ptr [rcx + 4*rax]
    add rax, rcx
    jmp rax
case0:
    mov edi, 20
    jmp finish
case1:
    mov edi, 21
    jmp finish
case2:
    mov edi, 22
finish:
''' + EXIT + '''
.p2align 2
table:
    .long case0 - table
    .long case1 - table
    .long case2 - table
    .long case2 - table
'''
        binary, report = self.program(source)
        self.assertEqual(report['jump_tables'], {'qualified': 0, 'candidate': 0})
        self.assertIn('indirect-jump-dispatch', {item['kind'] for item in report['obligations']})
        result = self.run_binary(binary)
        self.assertIn(b'control transfer to an address without converted code', result.stderr)
        binary, report = self.program(source + 'end_of_start:\n', unwind=[('_start', 'table')])
        self.assertEqual(report['jump_tables'], {'qualified': 0, 'candidate': 1})
        self.assertIn('jump-table-extent-candidate', {item['kind'] for item in report['obligations']})
        result = self.run_binary(binary)
        self.assertEqual(result.returncode, 22, result.stderr)

    def test_flags_read_after_call_are_rejected(self):
        with self.assertRaisesRegex(guest_lift.LiftError, 'read after the call'):
            self.program(PRELUDE + '''
    call helper
    je zero
    mov edi, 1
''' + EXIT + '''
zero:
    xor edi, edi
''' + EXIT + '''
helper:
    test edi, edi
    ret
''')

    def test_flags_read_at_entry_are_rejected(self):
        with self.assertRaisesRegex(guest_lift.LiftError, 'read before definition at unit entry'):
            self.program('''
.globl _start
_start:
    setb al
    movzx edi, al
''' + EXIT)

    def test_unimplemented_instruction_rejects_conversion(self):
        with self.assertRaisesRegex(guest_lift.LiftError, 'unsupported instruction'):
            self.program(PRELUDE + '''
    vaddps ymm0, ymm1, ymm2
    xor edi, edi
''' + EXIT)


if __name__ == '__main__':
    unittest.main()
