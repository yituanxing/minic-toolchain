# 2026-10-23: current-main compiler capability recertification

## Scope and acceptance (not yet certified)

The four-backend MiniC restructuring and Git branch cleanup have merged to
`main`. The previous x86/RV64/AArch64/LoongArch64 certifications are historical,
so this *new* run explicitly rechecks the current compiler binaries.

### Three existing and new owners, zero cloned Linux build recipes

1. **RV64 Linux, original 6.6.143**: reuse existing
   `linux-runtime-gcc-baseline.yml` `gcc-shards` matrix
   `[0,1,2,3,4,5,6]` plus `gcc-shard-verify` receiver. Assert complete
   canonical 2,064 real MiniC C objects, SHA-checked seven shard union, GNU
   assemble/link, and QEMU P1 syscall/runtime. The opt-in commit tags are
   `[linux-runtime-gcc-sharded] [linux-runtime-full-crossblock]
   [linux-runtime-p1-defined-crossblock]`.
2. **AArch64 + LoongArch64**: reuse `toolchain-focused-regressions-v1.yml`
   `mode=all` one-build regression, which includes real tinyAES, cJSON, zlib,
   Lua, SQLite, TinyCC, and original BusyBox 581/581 MiniC C owners with
   SHA verification and 38 GNU-QEMU byte-exact fixtures **per ISA**.
3. **x86-64**: `minic-capability-recertify.yml` verifies pinned unmodified
   AES, cJSON, zlib, Lua, SQLite and TinyCC against native GNU, plus reports
   real upstream BusyBox 12-owner and current all-eligible-owner mixed Kbuild
   acceptance separately. Historical x86 partial BusyBox success **does not**
   mean all BusyBox C owners are MiniC; GNU fallback remains a subsequent task.

All verdicts must be taken from the **same current main commit** or a follow-up
source-identical CI commit. A successful workflow dispatch alone is not an
acceptance receipt; inspect job status, SHA, object counts and actual QEMU
output. The Linux golden-source fixture/cache must fail closed if missing.

### CI routing after cleanup

- Main source changes: native strict fast ownership/x86 ABI + focused owner
  suite; only relevant source-path pushes trigger them.
- Heavy Linux 2064 seven-shard acceptance: explicit commit tag or manual opt-in
  to the existing Linux workflow, **not** routine pushes.
- Full x86 original-source suite: manual or scoped YAML-change push.
- Pinned A/B benchmark: manual only.
- Historical Linux/ASM/linker probes: preserve original reusable/manual
  owners and archived YAML while confirming which contracts are still needed.
- Do not introduce a second 2064-shard implementation or conflate historical
  CI PASS with current-commit acceptance.

### Next phase gate

After current RV64 Linux QEMU P1 and the other architectures' six real-software
tests pass, repair any residual BusyBox original-all-C-owner gaps on x86-64.
Then requalify original BusyBox AArch64 and LoongArch64 and begin the Linux
cross-target bring-up **without silently falling back to GCC for C ownership**.
