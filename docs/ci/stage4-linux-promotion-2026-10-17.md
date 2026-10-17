# Stage4 MiniC → Linux 6.6.143 promotion (independent from toolchain selfhost)

## Pinned source and compiler identity

- The fully native **nine-tool Stage3→Stage4 self-build** passed in
  [CI #39058935623](https://github.com/yituanxing/minic-toolchain/actions/runs/39058935623).
  Rebuilt **77/77** production C TUs twice, linked **9/9** ELF tools at both
  stages, compared all `.i/.s/.o` and Stage3/4 ELF identities, passed the
  Stage4-built PP→CC→AS→LD→QEMU exit42 + Driver→QEMU exit42 gates.
- Exact **Stage4 RV64 `minic-cc` SHA256**:
  `50b3f7763bde2505e17bb545513cb00664286d4e762b3395aea3f29ce724ef31`.
- Original Stage0 compiler benchmark oracle SHA256:
  `f04794d3132631059c028e0a889bf8cf06f15e8f24afcc70d24697edce4925fd`.
- Do not silently substitute the older, separately certified Stage2 RV64 binary
  (SHA `17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d`).
- The Stage4 compiler executable was bootstrapped using binary-seed musl 1.2.5
  CRT/libc and an existing libgcc.a; this does **not** certify native libc build.

## Stage4 frozen first500 parity

- [CI #39059924681](https://github.com/yituanxing/minic-toolchain/actions/runs/39059924681).
- The **existing seven `shard` runners** divide the SHA-pinned,
  original Linux 6.6.143 500-`.i` frozen Kbuild source cache into
  **72+72+72+71+71+71+71** translation units.
- Reuse `tools/ci/minic-bootstrap-stage2-linux-sample-v1.py`,
  `tests/external/linux/corpus_replay.py`, and
  `tests/external/linux/assemble_replay.py` with explicit
  `--candidate-label stage4`, plus identical Stage0 reference.
- Required: frozen input SHA 500/500, real Stage4 vs Stage0 `.s` parity
  **500/500**, independent GNU assembler `.o` identity **500/500**.
  `kernel/configs.o` requires an explicitly documented empty `.incbin`
  test stub: that specific frozen parity test is **not a bootable Image**.
- `tools/ci/minic-bootstrap-stage4-linux500-shard-v1.sh` installs the
  attested Stage4 ELF under QEMU only; each shard preserves JSON results.
- A running/successful **individual** shard is not proof the entire first500
  passed. All seven must pass. **Never report a pass before CI completes.**

## Stage4 real Linux 2064 + QEMU P1

- [CI #39060225407](https://github.com/yituanxing/minic-toolchain/actions/runs/39060225407).
- Reuse the **existing seven `gcc-shards` producer jobs and one
  `gcc-shard-verify` receiver** from
  `.github/workflows/linux-runtime-gcc-baseline.yml`.
- Compile the exact **2064 real Linux C targets** with the certified
  Stage4 compiler running under `qemu-riscv64`; the existing GNU
  **preprocessor, assembler, final linker and Kbuild infrastructure remain
  frozen reference dependencies**, rather than being counted as native.
- `tools/ci/minic-bootstrap-stage4-install-kbuild-v1.sh` retains the
  original host Stage0 driver as a *dispatcher only* and requires its
  direct Stage4 compilation, wrapper compilation, and driver compilation
  to agree byte-for-byte before the first shard.
- `tools/ci/minic-bootstrap-stage4-2064-prepare-v1.sh` rejects source
  drift from the original 47-TU compiler profile and verifies the
  Stage4 ELF SHA against the certified nine-tool `stage4/summary.json`.
- Each producer must prove its Kbuild owner set, real guest QEMU Stage4
  invocations, and object SHA/ABI via the *existing* cohort pipeline and
  extended `minic-bootstrap-stage2-2064-shard-audit-v1.py
  --candidate-stage stage4`. The historic internal Kbuild intermediate
  suffix `.minic-stage2.s` is a **legacy path label**, not the compiler version.
- The receiver must merge **2064/2064** fully attested Stage4-produced
  objects, use the historical GNU linker, and pass **QEMU P1 12/12
  syscalls** with the original runtime oracle. Missing shard/trace,
  source drift, unexpected compiler hash or P1 failure must fail closed.
- This scope is **Stage4 C compiler promotion**, not the entire nine-tool
  chain natively building Linux; the latter is a separate milestone.

## Next irreversible claims

1. Only after all 500 first-slice objects pass can the 500 stage claim PASS.
2. Only after seven 2064 shard jobs and the runtime receiver pass can one
   claim **Stage4 MiniC real Linux 2064 + QEMU P1=PASS**.
3. For GNU PP/AS/LD removal in Linux itself, reuse separate already-tested
   MiniPP/MiniAS/MiniAR/MiniLD individual substitution suites; do not
   conflate the all-native toolchain selfhost with full native Linux.
4. Only a separate native musl/CRT/libgcc build establishes independence
   from external binary runtime seeds.

These checks deliberately reuse historical CI and test scripts rather than
inventing a second kernel build graph or silently changing compiler options.

## Execution log (run IDs are evidence, not promises)

- `#39059924681`: all seven actual frozen 500 Stage4 differential shard commands printed `BOOTSTRAP_STAGE4_LINUX500_SHARD=PASS`: 72+72+72+71+71+71+71; `.s` and GNU-AS `.o` identity each 500/500. The enclosing CI was **red for unrelated "Build latest Canonical MiniC" and metrics-export steps after all seven parity checks**, so report the exact per-shard evidence rather than labeling that workflow itself green. No Linux Image was linked.
- `#39062218853`: separate seven-shard rerun, isolated from unrelated perf3352 jobs; aggregate must only report PASS if all seven jobs and sha-verified parity reports pass. At latest inspection some shards still running, not yet a green aggregate.
- `#39063709251`: **FULL GREEN** independent Stage4 frozen Linux500 archive-only aggregate. Exactly seven previously green producer shard artifacts, all 500 unique C owners and hashed frozen inputs, **500/500 assembly SHA identical Stage0 vs Stage4** and **500/500 GNU-AS object SHA identical**. Logs: `BOOTSTRAP_STAGE4_LINUX500=PASS owners=500/500 stage0_stage4_asm=500/500 rv64_objects=500/500`. Aggregate artifact `minic-bootstrap-stage4-linux500-final` id **12112059073**; the aggregate reused CI `#39062218853` artifacts and **compiled zero new Linux files**. The original seven-shard CI had all 7 producer jobs green, but the aggregate erroneously looked for `stage4_assembly_sha` in candidate-keyed JSON and failed; fixed JSON key and separately triggered the independent aggregate without repeating the 500 compilations. No Linux Image/QEMU kernel P1 claim at this gate.
- `#39060225407`: first full Stage4 Linux 2064 producer shard 0 compiled **295/295 real C objects** in 2,024.970 seconds with `COHORT_ABI=PASS`, and Stage4 guest/host-driver SHA-smoke PASS. A **new owner-success audit** stopped it because the existing Kbuild append-only success trace included **two ancillary MiniC compilations beyond canonical 295 owner objects**. This is an audit logic mismatch, not an observed compiler failure; the other six shards and receiver remained unverified.
- `833d2c3b`: owner-audit change to require *all* canonical targets, reject missing guest traces and allow only a bounded, individually guest-traced ancillary set. This is stricter than silently skipping the check. Stage2 legacy exact-owners mode remains unchanged.
- `#39063243332`: attempt to restart full 2064 failed at YAML validation before jobs because a CI-only comment was under-indented inside a `run: |` block. No compiler build ran; repaired by `dc8aefdd`.
- `#39063287653`: restored valid workflow trigger for all seven real Stage4 2064 producer jobs and existing QEMU P1 receiver. Do not claim Linux 2064 or P1 pass until the new jobs attest them.
