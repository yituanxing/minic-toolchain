# AArch64 NEON: honest six-owner closure plan

Linux 6.6.143 AArch64 P2 GNU-preprocessed `C` census: **4371/4377 MiniC
assembly passes**. The remaining six genuine owners are:

1. `arch/arm64/lib/xor-neon.o`
2. `lib/raid6/neon1.o`, `neon2.o`, `neon4.o`, `neon8.o`
3. `lib/raid6/recov_neon_inner.o`

No Kconfig workaround keeps the same owner census: `KERNEL_MODE_NEON` is
`def_bool y` in `arch/arm64/Kconfig`, while `XOR_BLOCKS` and `RAID6_PQ` are
selected by filesystems such as Btrfs. Disabling those dependencies changes the
configuration and removes selected owner objects; it is **not** 4377/4377.

## Verified completed gates

- GCC `push_options` / `target("+nothing+simd")` / `pop_options` have a parser
  stack with AArch64-owned exact admission. All other target extensions are
  rejected; `+simd` is **not** a MiniC vector-codegen capability.
- The source identities `__Uint8x16_t`, `__Int8x16_t`, `__Poly8x16_t`,
  `__Uint64x2_t` have target-owned **16-byte C storage and 16-byte alignment**.
  Other ISAs reject those names. This does not enable vector SSA values,
  operator semantics, call ABI or inline-asm constraints.
- `tests/compiler/c0/run-aarch64-neon-stage2.sh` uses the **same GNU-preprocessed
  layout translation unit** for GNU and MiniC; both link and execute under
  QEMU. An independent GCC `<arm_neon.h>` load/XOR/store program runs under
  QEMU. The same test additionally verifies GCC accepts vector ALU/ABI source
  while MiniC **rejects** `^` on vector values, by-value vector returns, and
  the original GCC `arm_neon.h` pragma. These explicit negative tests must
  flip to positive runtime tests only when the corresponding ISA/ABI stages
  have real machine implementations.

## Required next gates (must pass sequentially)

1. **Vector128 expression/IR**: add one target-neutral `fixed_vector128`
   classification, preserving the four distinct lane/polynomial C identities;
   verify casts are bit-preserving. Admit only same-identity `^` initially.
   Add a typed Core vector-XOR opcode, 128-bit SSA value/temporary storage,
   and exact 16-byte load/store effects. Do **not** broaden existing
   `core_memory_scalar_type()` globally: its users include scalar calling
   conventions and inline assembly and would silently claim an ABI.
2. **AArch64 native lowering**: Q-register frame load/store already exists
   for long double (`aarch64/frame_emit.c`, `memory_emit.c`), while shared
   Core spill planning already reserves two 8-byte units for a 16-byte value.
   Reuse these audited mechanisms, but preserve semantic vector identity.
   Emit `ldr/str qN`, `eor vD.16b,vN.16b,vM.16b`. Verify real output bytes,
   assembler opcodes and GCC-vs-MiniC/QEMU differential executions, including
   unaligned pointers and aliasing. Do not map vector operations through an
   `__int128` or record shortcut.
3. **Front-end GCC intrinsic-header semantics**: the GCC 13 `arm_neon.h`
   begins with `#pragma GCC aarch64 "arm_neon.h"`, which registers builtin
   types and operations. That directive cannot be silently discarded. It
   declares many distinct vector types and thousands of intrinsic definitions.
   Either implement the real GNU preprocessed `.i` admission (including
   target-owned builtin signatures) or distribute a semantically equivalent
   MiniC-owned ACLE header and validate a separate real MiniC-preprocessed
   kernel build. Only the first route preserves the exact **GNU `.i`** frontier.
4. **Owner-specific NEON intrinsics**: `xor-neon` needs `vld1q_u64`,
   `veorq_u64`, `vst1q_u64`; the four `raid6/neonN` owners additionally use
   `vld1q_u8`, `vst1q_u8`, `veorq_u8`, `vdupq_n_u8`, `vandq_u8`,
   signed/unsigned byte shifts, polynomial byte multiply and vector casts;
   `recov_neon_inner` adds `vqtbl1q_u8` byte-table permutation. Implement
   each exact lane semantics as target-owned machine intrinsics, not stubs.
5. **AAPCS64 and native inline assembly**: implement and test actual V0-V7
   vector argument/return registers, 16-byte local stack objects, HVA
   classification/callee-saved rules, and `"=w"`/`"w"` vector constraint
   bindings. `xor-neon.c` has an SHA3 `eor3 %0.16b,...` ASM template; it
   must execute the SHA3 instruction under an appropriate QEMU CPU or be
   validly guarded by the original kernel runtime feature test. C source
   feature permission is not a machine-emission capability.
6. **Strict owner census**: replay all **six original GNU `.i` owners** in
   one immutable config, then cross-assemble/link and boot/function-test with
   GNU and MiniC. The denominator stays 4377 throughout; exact six must go
   from FAIL to PASS based on real emitted ELF and runtime behavior.

Implementation principle: Frontend parses generic syntax; `TargetInfo` owns
source admission and type layout; Core carries target-neutral typed semantics;
the AArch64 backend alone owns V-register allocation, ISA instructions, and
ABI. A parse-only success is never reported as a vector runtime pass.

## Cross-architecture caveat: x86 KVM SSE128 is *storage*, not permission

Linux `arch/x86/kvm/fpu.h` has `typedef u32 __attribute__((vector_size(16)))
sse128_t`. The x86 kernel builds with `-mno-sse -mno-mmx -mno-sse2
-mno-3dnow -mno-avx` (`arch/x86/Makefile`); GNU nevertheless preserves the
16-byte vector type's layout and can copy it with two 64-bit GPR operations.
The Linux inline-ASM `movdqa` uses an explicit 16-byte memory operand and
an assembler-owned XMM register, not a compiler-selected SSE arithmetic
register. Therefore a future **generic** GNU `vector_size` type must carry
lane base/count, byte extent, alignment, and a distinct vector type identity,
while three capabilities remain independent:

1. source vector **declaration/storage** and byte-exact copy;
2. vector **ALU/register return/argument ABI** and target ISA options;
3. width-correct **inline-ASM memory operands**, without granting general
   SSE emission.

Even after this source-layout gate, the existing x86 machine ASM classifier
(`src/target/x86_64/asm_contract.c`, `machine_asm.c`) does **not** recognize
KVM's `movdqa` transfer shape; its i915 `movntdqa`/`movaps` cases are not a
general substitute. A separate exact template/`=m`-memory-form/Core emission
contract must validate 16-byte alignment, memory direction and XMM operand
without broadening `-mno-sse` codegen. Thus x86 KVM remains FAIL until that
gate has its own native GNU/QEMU differential evidence.

The four AArch64 builtin vector identities in the current first storage gate
are intentionally not used as a fake generic `vector_size` representation.
Adding the generic form requires extending the type identity and parser GNU
attribute contract, with explicit x86 target admission and negative tests
that vector ALU remains disabled under Linux's `-mno-sse` profile.
