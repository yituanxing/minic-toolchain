# RV32I native two-XLEN integer arithmetic — acceptance boundary

After the RV32/RV64 ownership split, the ILP32 psABI plan and machine
operation families are stable. This round adds missing real C language
semantics in one coherent target-only extension rather than continuing
to split existing files.

The dedicated `rv32_wide_emit.c` is responsible for 64-bit two-word
arithmetic at XLEN=32: carry/borrow addition and subtraction, bitwise
ops, two's-complement negate/not, full-width signed/unsigned less,
scalar equality/zero, and dynamic left/logical-right/arithmetic-right
shifts including 0/31/32/33/63 word-boundaries. The existing SSA
value/frame layout and incoming/outgoing ABI Plan remain unchanged.

MiniC now emits standard compiler runtime ABI calls for 64-bit
multiply, signed and unsigned divide and remainder:
`__muldi3`, `__divdi3`, `__udivdi3`,
`__moddi3`, `__umoddi3`. Their original LLVM compiler-rt
19.1.7 sources, including `udivmoddi4.c` and
`int_div_impl.inc`, were fetched unchanged from upstream tag
`llvmorg-19.1.7`. The original GNU libgcc assembly remains
responsible for the 32-bit `SI` helper family.

There is no handwritten division/multiplication algorithm in MiniC,
only ILP32 two-XLEN argument placement, runtime ABI symbol selection
and result reception. The base-I instruction profile is preserved.
A two-word shift *count* operand still fails closed to prevent
silently truncating unsupported widths.

The dedicated QEMU differential compiles the same source using
GCC RV32I/ILP32 and MiniC, assembles MiniC .s through both GNU AS
and MiniAS, links against exactly the same original vendor helpers
and tests the full 64-bit results against an independently compiled
GNU C driver on 10x10 diverse 64-bit bit patterns, nonzero divisors, signed
minimum, signed overflow exclusions, and seven shift boundaries.
This is capability qualification, not proof all RV32 C constructs or
the complete cJSON runtime have passed.

The already accepted RV64I Linux seven-shard origin and four
mature 64-bit backend runtime evidence must remain green.
