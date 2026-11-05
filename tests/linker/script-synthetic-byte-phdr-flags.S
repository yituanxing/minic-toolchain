.text
.globl _start
.type _start, @function
_start:
    mov $60, %eax
    xor %edi, %edi
    syscall
.size _start, .-_start
.data
.globl writable_payload
writable_payload:
    .quad 0x1020304050607080
