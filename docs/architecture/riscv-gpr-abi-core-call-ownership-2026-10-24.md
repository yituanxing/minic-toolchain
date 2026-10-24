# RISC-V argument placement / Core call ownership audit (2026-10-24)

Baseline: `6cfea41cf521100d57c8a1dd6faef80c3f3a0245`.

Dependency review: repository source/build/CI layout, five target entry
points, Core IR verified call identity, RV32 frames/calls, RV64 LP64[DF]
classification and call emission, RISC-V ISA selection, common Core
call view, the real-program GNU/GCC/QEMU suites. Not a claim that every
line of the entire repository was read independently.

## Duplicated ownership removed

- RV32 previously decoded Core direct and indirect callee signatures in
  `rv32_call_info`. It now uses the same checked `MinicCoreCallView`
  already consumed by mature x86/RV64/AArch64/LoongArch paths, both for
  outgoing frame planning and for machine call emission.
- RV32 and RV64 had separate copies of a0..a7 integer-register versus
  XLEN-sized stack-slot placement. A pure shared GPR psABI cursor now
  owns that algorithm. RV32 preserves its public ILP32 cursor as a
  policy adapter and RV64 retains its independent type classification,
  optional floating-register banks and per-function ABI plan.
- Unnamed 2-XLEN varargs requiring even pairs are never split. On
  register exhaustion, the pair starts at an aligned stack slot and
  later unnamed args stay on stack. Named wide arguments may split
  across a7 and stack0. The shared algorithm detects size overflows
  without mutating the caller cursor.

```
Core CALL / INDIRECT_CALL
           |
   common/core_call_view
           |
    RV32 classifier               RV64 classifier
   (i64/DF = 2 words)          (i128 = 2 words)
       |                             |
       +-- riscv/gpr_abi_cursor.h ---+
        (a0..a7 + XLEN stack words)
       |                             |
    RV32 ILP32 frame/emitter     RV64 LP64 frame/emitter
```

No physical frame sizes, data model, machine ISA, hard-float register
allocation or runtime implementation were unified. The remaining
independent RV32 and RV64 frame algorithms should only converge after
reviewing value lifetimes, alignment and stack restoration across
Linux, BusyBox and original RISC-V real-software programs.

Evidence gates: standalone GPR psABI placement test; RV32 original GCC
va_start/va_arg, GNU-AS/MiniAS ELF32 QEMU differential, untouched AES
vectors/cJSON; RV64 i128 and softfloat GNU/QEMU; x86/AArch64/LoongArch64
original runtime; RV64I seven-shard origin checks. Existing MiniLD
.riscv.attributes failure remains independently tracked, not masked.
