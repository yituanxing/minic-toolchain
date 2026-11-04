/* A static IFUNC is a resolver, not an ordinary function/data address.
 * Do not emit a GOT slot containing its resolver address as a final value. */
.text
.globl _start
.type _start,@function
_start:
    movq fixture_ifunc@GOTPCREL(%rip),%rax
    mov $60,%eax
    xor %edi,%edi
    syscall
.size _start,.-_start

.globl fixture_ifunc
.type fixture_ifunc,@gnu_indirect_function
fixture_ifunc:
    ret
