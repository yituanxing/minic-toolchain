# MiniC RISC-V/LoongArch64 ABI second-round review (2026-10-29)

Scope: `src/target/riscv64/{abi.c,function_abi_plan.c,legalize.c}`, LoongArch64
`memory_emit.c`, and target-owned tests. These changes are **not** a proof of
cross-target runtime correctness. Evidence types are distinguished below.

## Confirmed and addressed

1. **RV64 LP64D named FP9 cursor: corrected.** Before the patch,
   `abi.c` returned `false` at exactly eight exhausted FP argument registers;
   strict preprocessing and `-S` failed at the first function `ninth`.
   The amended cursor assigns `fa0..fa7`, then resumes the independent
   a-register/stack sequence. `function_abi_plan.c` admits a qualifying
   named FLOAT at its one-GPR/stack fallback location;
   `legalize.c` shares a single scalar-GPR admission helper across direct and
   indirect calls. `probe-rv64-hardfloat-ninth-fixed.sh` validates named
   double9 -> a0, mixed double9/10 -> a1/a2, exhausted GAR -> stack0,
   float9 -> a0, indirect double9, and the non-F/D strict LP64 profile:
   **strict `-S` PASS**. Unit `check-rv64-abi` **PASS**.

2. **RV64 FP-bearing named record: unsafe prior acceptance rejected.** Before
   the patch, the strict LP64D compiler accepted
   `struct OneFloat { float f; }` and emitted `receive_one_float` reading its
   source from `a0` **as a pointer**; a call site likewise generated
   `addi a0,sp,4` before `call receive_one_float`. Under the RV psABI the
   named floating-point aggregate is flattened into an FPR; mixed float/int
   uses a combined FP/GP location. This was a real ABI incompatibility, not
   just an unsupported edge case. `abi.c` now fails closed for FP-containing
   records <=16B in the hard-float named/return profile; pure-integer
   records and LP64 softfloat keep their prior classification. Unnamed LP64D
   variadic arguments use the integer aggregate convention, including
   16-byte alignment to an even GAR pair. GNU C valid FLT/DBL/mixed fixtures
   now receive a compiler diagnostic and produce no partial `.s`:
   `probe-rv64-hardfloat-fp-record-reject.sh` **PASS**. **P0 residual:** the
   full LP64D aggregate flattening/return model remains to be implemented;
   these legal named record signatures are intentionally rejected.

3. **LoongArch64 packed scalar/pointer/wide access: guarded machine
   operations.** Original strict `-S` of
   `loongarch64_packed_bitfield_alignment.c` showed `p+1` followed by
   unguarded `ld.w`, and packed 16-byte (`unsigned __int128`) access followed
   by unconditional `ld.d/st.d` at `p+1` and `p+9`. `memory_emit.c` now
   recognizes compile-time proof roots only from local object/global storage
   and checked offset/stride derivations; caller-supplied `PARAMETER` and
   reloaded pointers are **not** trusted merely because their C type suggests
   alignment. For unproved addresses, a native load/store is guarded by
   address `% width`, with a bounded, small byte-gather/scatter loop in the
   slow path. Wide 16B values apply the guard independently to both eightbyte
   halves and reload the original pointer before the second transfer. Atomic
   instructions keep their hardware-width operation and existing alignment
   preconditions. `probe-loongarch64-packed-scalar-alignment.sh` **strict
   `-S` PASS**: packed scalar/pointer/16B guards and byte fallback;
   naturally aligned static global 16B emits both native `ld.d` without a
   dynamic guard. The companion Core packed bit-field exact-byte extent fix
   (owned by the Core agent) prevents a separate end-of-object overread:
   a field at offset3, bit6, width10 must access two bytes, not four.

4. **LoongArch64 indirect record-result CI reject gate was stale.** The
   old `probe-loongarch-indirect-record-return-qemu.sh` expected failure from
   `struct Large call_factory(...) { return factory(...); }`, although
   `call_emit.c` already emits a hidden outgoing `$a0` buffer, shifted `$a1`
   parameter, `jirl` through function pointer, and restores the original
   incoming `$a0` before exact-extent copy-out. The stale rejection caused
   `LOONGARCH64_SRET=FAIL unsafe aggregate ABI accepted: indirect-callee`.
   The test now demands positive -S evidence, while retaining the >2048B
   rejection. A standalone `probe-loongarch64-indirect-sret-qemu.sh` supplies
   17/24/40-byte indirect-result fixtures plus GNU caller/callee executable;
   **strict -S PASS**, GNU cross/static/QEMU runtime **SKIP** locally because
   `loongarch64-linux-gnu-gcc-13` and `qemu-loongarch64` are absent. CI must
   execute this additional mixed-runtime oracle before declaring full proof.

## Validation, limitations, follow-up

- Independent full debug compiler build with `CFLAGS=-Werror` **EXIT0**;
  `check-rv64-abi` **PASS**.
- New strict -S probes: `RV64_HARDFLOAT_NINTH`,
  `RV64_HARDFLOAT_FP_RECORD`, `LOONGARCH_PACKED_ALIGNMENT`,
  `LOONGARCH64_SRET_SOURCE`, `LOONGARCH64_INDIRECT_SRET_SOURCE`: **PASS**.
- Legacy strict -S: RV64 packed 2/4/8 record byte-copy, irregular
  3/5/6/7/9/15, and RV32 variadic natural align4/align8: **PASS**.
- **No local QEMU dynamic runtime** for RV64 or LoongArch64 was executed.
  Architecture GNU linker/runtime conformance remains an explicit CI gate.
- **P1 performance/architecture follow-up:** `memory_emit.c` contains a
  conservative target-local alignment proof. Unknown pointer loads/stores
  carry a native-aligned branch plus compact byte fallback. There is no
  validated benchmark A/B yet; the branch has nonzero size/cycle cost. A
  future target-independent `AddressAlignmentProof` in the Core memory model
  can elide more guards without trusting naked pointer types; compare
  Linux file/time distributions before treating the fast path as complete.
- **P1 ABI follow-up:** implement the full LP64D flattened record model
  (one FP, two FP, FP+GP, fallback when FPR exhausted) in one cross-consumed
  cursor/return location plan, then replace deliberate FP-record rejections
  with GNU cross-built runtime probes. Do not bypass fail-closed diagnostics.

RISC-V primary reference: [official psABI calling convention](https://docs.riscv.org/reference/abi/v1.0/riscv-cc-procedure-calling-convention.html).
