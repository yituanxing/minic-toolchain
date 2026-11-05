/* GNU AS -mrelax-relocations=no: true R_X86_64_GOTPCREL, not GOTPCRELX.
 * The load must read the address stored in an actual GOT entry. */
.text
.globl _start
.type _start,@function
_start:
    movq fixture_value@GOTPCREL(%rip),%rax
    movq (%rax),%rax
    cmpq $42,%rax
    jne fixture_fail
    mov $60,%eax
    xor %edi,%edi
    syscall
fixture_fail:
    mov $60,%eax
    mov $47,%edi
    syscall
.size _start,.-_start

.data
.globl fixture_value
.type fixture_value,@object
fixture_value:
    .quad 42
.size fixture_value,8
