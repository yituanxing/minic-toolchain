/* Minimal definitions for real GNU crt1.o and crti.o.  The test deliberately
 * keeps the CRT's GOTPCRELX references and weak __gmon_start__, while avoiding
 * glibc so the admission/relaxation contract is isolated from libc's TLS/GOT. */
.text
.globl main
.type main,@function
main:
    xor %eax,%eax
    ret
.size main,.-main

.globl __libc_start_main
.type __libc_start_main,@function
__libc_start_main:
    mov $60,%eax
    xor %edi,%edi
    syscall
    ud2
.size __libc_start_main,.-__libc_start_main
