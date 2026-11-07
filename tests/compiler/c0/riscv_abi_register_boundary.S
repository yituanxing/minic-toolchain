/* GNU AS physical-register witnesses: no compiler normalization masks a
 * caller's raw a0 or ninth outgoing stack slot. */
.text
.globl _start
.type _start,@function
_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    call main
    li a7,93
    ecall

#if ABI_RV64
.globl abi_raw_arg_u32
.type abi_raw_arg_u32,@function
abi_raw_arg_u32:
    /* Return full incoming physical a0 bit pattern unchanged. */
    ret

.globl abi_raw_stack_u32
.type abi_raw_stack_u32,@function
abi_raw_stack_u32:
    /* Eight preceding unsigned-long arguments occupy a0..a7. */
    ld a0,0(sp)
    ret

.globl abi_raw_call_ret_u32
.type abi_raw_call_ret_u32,@function
abi_raw_call_ret_u32:
    addi sp,sp,-16
    sd ra,8(sp)
    call minic_ret_u32
    ld ra,8(sp)
    addi sp,sp,16
    /* Return MiniC's untouched raw unsigned-i32 a0 as unsigned long. */
    ret
#endif
