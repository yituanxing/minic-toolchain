.text
.option norelax
.globl _start
.type _start,@function
_start:
    la.local $r12,weak_optional_data
    la.local $r13,weak_optional_function
    ori $r4,$r0,0
    ori $r11,$r0,93
    syscall 0
.size _start,.-_start

/* GNU's undefined-weak resolution must agree for NOTYPE and FUNC. */
.weak weak_optional_data
.weak weak_optional_function
.type weak_optional_function,@function
