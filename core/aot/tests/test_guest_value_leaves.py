import functools
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import guest_emit
import guest_lab
import guest_lift
import guest_value_leaves


LAUNCHER = r'''#include "native/NativeAotGuest.hpp"
#include "native/NativeAotGlibc.hpp"
extern "C" const NativeAotGuestImage __aps5_guest_image;
extern char** environ;
void Promote(NativeAotGuestState& state) { NativeAotGuest::Return(state, 0); }
int main(int argc, char** argv) {
    NativeAotGlibc::Register();
    const NativeAotGuest::ProviderBinding binding{"promote", Promote, nullptr};
    NativeAotGuest::RegisterProviders(&binding, 1);
    NativeAotGuest::Start(__aps5_guest_image, argc, argv, environ);
}
'''


class GuestValueLeafTests(unittest.TestCase):
    def execute(self, body, helpers, imports=('exit',), launcher=None):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source = '.globl _start\n_start:\n    and rsp, -16\n' + body + '''
    xor edi, edi
    call qword ptr [rip + exit_slot]
fail:
    mov edi, 71
    call qword ptr [rip + exit_slot]
''' + helpers
            source += ''.join(f'\n.p2align 3\n{name}_slot: .quad 0\n' for name in imports)
            code, labels = guest_lab.assemble_program(source, folder)
            raw = guest_lab.program_image(code, labels, [(name, f'{name}_slot') for name in imports])
            with patch('guest_emit.build', functools.partial(guest_emit.build, native_leaf_values=True)):
                binary, report = guest_lab.build_program(raw, folder, concurrent=True, memory_epochs=True, launcher=launcher)
            result = subprocess.run([str(binary)], capture_output=True, timeout=10)
            self.assertEqual((result.returncode, result.stdout, result.stderr), (0, b'', b''))
            return report, labels, (folder / 'converted/shard00.ll').read_text()

    def test_frame_save_alias_is_loaded_after_private_and_ordered_body(self):
        report, labels, _ = self.execute('''
    mov r13, rsp
    mov rbp, 123
    mov edi, 91
    lea rsi, [rsp - 16]
    call leaf
    cmp rbp, 124
    jne fail
    cmp rax, 91
    jne fail
    cmp rsp, r13
    jne fail
    call qword ptr [rip + promote_slot]
    mov edi, 92
    lea rsi, [rsp - 16]
    call leaf
    cmp rbp, 125
    jne fail
    cmp rax, 92
    jne fail
    cmp rsp, r13
    jne fail
''', '''
leaf:
    push rbp
    mov rbp, rsp
    add qword ptr [rsi], 1
    mov rax, rdi
    pop rbp
    ret
''', imports=('exit', 'promote'), launcher=LAUNCHER)
        contract = next(item for item in report['native_leaf_contracts'] if item['address'] == guest_lab.CODE + labels['leaf'])
        self.assertTrue(contract['frame'])
        self.assertEqual(contract['outputs'], ('rax',))

    def test_return_slot_alias_uses_changed_target_without_replay(self):
        self.execute('''
    mov rbp, 123
    mov r13, rsp
    xor r10d, r10d
    xor eax, eax
    test eax, eax
    jne other
    inc r10
    lea rdi, [rsp - 8]
    lea rsi, [rip + success]
    call redirect
    jmp fail
other:
    call noop
success:
    cmp rax, 73
    jne fail
    cmp rbp, 123
    jne fail
    cmp rsp, r13
    jne fail
    cmp r10, 1
    jne fail
''', '''
redirect:
    push rbp
    mov rbp, rsp
    mov qword ptr [rdi], rsi
    mov eax, 73
    pop rbp
    ret
noop:
    ret
''')

    def test_bare_branch_alias_and_indirect_entry_preserve_state(self):
        report, labels, _ = self.execute('''
    sub rsp, 16
    mov r12, rsp
    mov dword ptr [r12], 7
    mov dword ptr [r12 + 4], 9
    mov rdi, r12
    mov rdx, r12
    mov esi, 1
    call leaf
    cmp eax, 73
    jne fail
    mov rdi, r12
    lea rdx, [r12 + 4]
    xor esi, esi
    call leaf
    cmp eax, 73
    jne fail
    cmp dword ptr [r12 + 4], 9
    jne fail
    call qword ptr [rip + promote_slot]
    mov rdi, r12
    lea rdx, [r12 + 4]
    mov esi, 1
    call leaf
    cmp eax, 74
    jne fail
    cmp dword ptr [r12 + 4], 73
    jne fail
    lea rax, [rip + leaf]
    call rax
    cmp eax, 75
    jne fail
''', '''
leaf:
    test esi, esi
    je skip
    inc dword ptr [rdi]
    mov dword ptr [rdx], 73
skip:
    mov eax, dword ptr [rdi]
    ret
''', imports=('exit', 'promote'), launcher=LAUNCHER)
        contract = next(item for item in report['native_leaf_contracts'] if item['address'] == guest_lab.CODE + labels['leaf'])
        self.assertFalse(contract['frame'])

    def test_unqualified_frames_and_helpers_stay_on_state_abi(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source = '''
.globl _start
_start:
    call leaf
    call changed_stack
    call bypass_frame
    call query
    call recursive
    ret
leaf:
    test esi, esi
    je skip
    inc dword ptr [rdi]
skip:
    mov eax, dword ptr [rdi]
    ret
changed_stack:
    add rsp, 8
    ret
bypass_frame:
    push rbp
    mov rbp, rsp
    test edi, edi
    jne early
    pop rbp
early:
    ret
query:
    cpuid
    ret
recursive:
    test edi, edi
    je finish
    dec edi
    call recursive
finish:
    ret
'''
            code, labels = guest_lab.assemble_program(source, folder)
            raw = guest_lab.program_image(code, labels, [])
            units, _, _ = guest_lift.discover(guest_lift.Program(raw))
            contracts = guest_value_leaves.candidates(guest_emit.Lowering, units, 'atomic')
            for name in ('changed_stack', 'bypass_frame', 'query', 'recursive'):
                self.assertNotIn(guest_lab.CODE + labels[name], contracts)
            address = guest_lab.CODE + labels['leaf']
            core, _, _, _ = guest_value_leaves.emit(guest_emit.Lowering, units[address], units, contracts[address], True, 'atomic')
            self.assertNotIn('ptr %s', core)
            self.assertIn('load atomic volatile i32', core)
            self.assertIn('store atomic volatile i32', core)
            self.assertIn('dmb ish', core)


if __name__ == '__main__':
    unittest.main()
