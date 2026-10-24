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

For 64-bit multiply, emit only the compiler runtime ABI call
`__muldi3`, supplied by **unaltered original LLVM compiler-rt
19.1.7 muldi3.c** built by the established RV32 freestanding
vendor archive. No duplicated long-multiply implementation exists in
MiniC; other 32-bit multiply/divide paths continue to use the original
GNU libgcc assembly. All generated instructions use RV32I base-I,
with no implicit M/F/D requirements.

64-bit divide/remainder are still fail-closed: the vendored RISC-V
GNU `div.S` is specifically a 32-bit helper when __riscv_xlen=32;
silently calling it for 64-bit C would truncate. These require a
pinned, independently validated two-word runtime provider before
activation.

The dedicated QEMU differential compiles the same source using
GCC RV32I/ILP32 and MiniC, assembles MiniC .s through both GNU AS
and MiniAS, links against exactly the same original vendor helpers
and tests the full 64-bit results against an independently compiled
GNU C driver on 8x8 diverse 64-bit bit patterns and shift boundaries.
This is capability qualification, not proof all RV32 C constructs or
the complete cJSON runtime have passed.

The already accepted RV64I Linux seven-shard origin and four
mature 64-bit backend runtime evidence must remain green.
