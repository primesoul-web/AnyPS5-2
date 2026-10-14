import functools
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import guest_emit
import guest_lab


class GuestCallEffectsTests(unittest.TestCase):
    def execute(self, body, helpers='', imports=('exit',), launcher=None):
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
            with patch('guest_emit.build', functools.partial(guest_emit.build, call_effects=True)):
                binary, _ = guest_lab.build_program(raw, folder, concurrent=True, memory_epochs=True, launcher=launcher)
            result = subprocess.run([str(binary)], capture_output=True, timeout=10)
            self.assertEqual((result.returncode, result.stdout, result.stderr), (0, b'', b''))

    def test_recursive_calls_preserve_partial_and_vector_registers(self):
        self.execute('''
    mov r12, rsp
    sub rsp, 64
    mov r13, 918
    mov rbx, 1234605616436508552
    mov r10, 33
    vmovups ymm0, ymmword ptr [rip + data]
    vmovups ymm1, ymmword ptr [rip + data]
    call wrapper
    cmp r10, 36
    jne fail
    mov rax, 1234605616436508586
    cmp rbx, rax
    jne fail
    cmp r13, 918
    jne fail
    vmovups ymmword ptr [rsp], ymm0
    cmp qword ptr [rsp], 52
    jne fail
    cmp qword ptr [rsp + 8], 0
    jne fail
    cmp qword ptr [rsp + 16], 0
    jne fail
    cmp qword ptr [rsp + 24], 0
    jne fail
    vmovups ymmword ptr [rsp + 32], ymm1
    cmp qword ptr [rsp + 32], 11
    jne fail
    cmp qword ptr [rsp + 56], 44
    jne fail
    add rsp, 64
    cmp rsp, r12
    jne fail
''', '''
wrapper:
    mov ecx, 3
    call recursive
    mov bl, 170
    mov eax, 52
    vmovd xmm0, eax
    ret
recursive:
    inc r10
    sub ecx, 1
    je finished
    call recursive
finished:
    ret
.p2align 5
data: .quad 11, 22, 33, 44
''')

    def test_nonlocal_return_keeps_changed_state_at_resume_point(self):
        self.execute('''
    mov r13, 17
    xor r10d, r10d
    xor eax, eax
    test eax, eax
    jne other
    inc r10
    call redirect
    jmp fail
other:
    call noop
success:
    cmp r13, 99
    jne fail
    cmp r10, 1
    jne fail
''', '''
redirect:
    mov r13, 99
    lea rax, [rip + success]
    mov qword ptr [rsp], rax
    ret
noop:
    ret
''')

    def test_cpuid_helper_materializes_external_register_writes(self):
        self.execute('''
    mov eax, 4294967295
    mov ebx, 29
    mov ecx, 37
    mov edx, 41
    call query
    or eax, ebx
    or eax, ecx
    or eax, edx
    test eax, eax
    jne fail
''', '''
query:
    cpuid
    ret
''')

    def test_opaque_provider_promotion_keeps_transitive_register_writes(self):
        launcher = r'''#include "native/NativeAotGuest.hpp"
#include "native/NativeAotGlibc.hpp"
extern "C" const NativeAotGuestImage __aps5_guest_image;
extern char** environ;
void Foreign(NativeAotGuestState& state) {
    state.Gpr[10] = 73;
    NativeAotGuest::Return(state, 0);
}
int main(int argc, char** argv) {
    NativeAotGlibc::Register();
    const NativeAotGuest::ProviderBinding binding{"foreign", Foreign, nullptr};
    NativeAotGuest::RegisterProviders(&binding, 1);
    NativeAotGuest::Start(__aps5_guest_image, argc, argv, environ);
}
'''
        self.execute('''
    mov r10, 17
    mov r13, 19
    call wrapper
    cmp r10, 73
    jne fail
    cmp r13, 20
    jne fail
''', '''
wrapper:
    call provider
    inc r13
    ret
provider:
    call qword ptr [rip + foreign_slot]
    ret
''', imports=('exit', 'foreign'), launcher=launcher)


if __name__ == '__main__':
    unittest.main()
