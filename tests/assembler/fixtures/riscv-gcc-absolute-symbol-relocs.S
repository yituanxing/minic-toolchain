.option norelax
.attribute arch, "rv64i2p1_m2p0_a2p1_f2p2_d2p2_c2p0_zicsr2p0_zifencei2p0"
.attribute unaligned_access, 0
.attribute stack_align, 16
.text
.globl rv_gcc_absolute_symbols
rv_gcc_absolute_symbols:
    lui a5, %hi(runtime_bias)
    addi a5, a5, %lo(runtime_bias)
    ld a4, %lo(runtime_bias)(a5)
    sd a4, %lo(runtime_bias)(a5)
    fld fa0, %lo(runtime_bias)(a5)
    fsd fa0, %lo(runtime_bias)(a5)
    lui a0, %hi(runtime_bias+8)
    addi a0, a0, %lo(runtime_bias+8)
    ret
