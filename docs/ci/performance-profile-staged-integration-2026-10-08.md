# Opt-in performance integration on Linux Runtime / 性能集成候选

## Version identity (2026-10-08)
- Runtime base: `17f8a4dcc3b2ec700b9f5ea8f717494d820e17e7`.
- Previously verified performance source: `104ccf9b00e0061105c3b98dd879c41931aa3d48`.
- The existing **production** `tools/ci/linux-runtime-build-minic-profile-v1.sh` and its 39-patch default are **unchanged**. Linux Image/QEMU cache ownership and certification should remain tied to this old runtime profile until re-certified.
- Candidate: `tools/ci/linux-perf-build-minic-profile-v1.sh` (55 patches), **for isolated disposable checkouts only**; do not call in a dirty developer workspace.

## What was imported

1. Exactly 16 additional, known-version performance and correctness patch scripts from the performance branch; existing profile script names and order are preserved.
2. Three modified prerequisite patch scripts (transitive-integer, base-callee-integer, capacity) as `tools/ci/perf-v1-overrides/` copies, not overwriting the default runtime variants.
3. All three performance-branch Parser source deltas reproduced as fail-closed, exact-context transformations by `tools/ci/apply-perf-parser-source-deltas-v1.py`. These are only applied within the opt-in profile, so default checked-in parser source files do not change.
4. Existing 500 TU paired A/B runner and two GNU `__builtin_constant_p` / `__builtin_choose_expr` regression source files.
5. Exactly two isolated integration workflows rather than permanently transplanting all historical performance experiments.

## Verified source-line evidence before transplant

- `37749097428`: candidate speedup 1.1272x on 500 paired units (baseline sum 790.452 TU-s, candidate 701.245 TU-s), 500/500 assembly matches within this paired experiment. This is **not** a legacy runtime-vs-new-candidate benchmark.
- `37749097415`: parser-scope isolated correctness/performance pairing 500/500 matched, 0.9959x speedup; scope fix has semantic value even though this run was not a speedup.
- `37741897092`: GNU constant-expression targeted GCC + RISC-V QEMU semantic regression PASS.
- `37740999109`: 3352/3352 Linux `.i -> .s` success on its certified earlier correctness branch. **Not** a full image/boot certificate for this transplanted runtime candidate.

## Integration gates on the runtime branch

- `.github/workflows/linux-runtime-optin-perf-first500-ab-v1.yml`: frozen first500 inputs, independently built paired candidates with and without the Boolean/Typedef optimization, 500/500 exact output comparisons and parser/QEMU smoke gates.
- `.github/workflows/linux-runtime-optin-perf-all3352-v1.yml`: independently rebuild candidate profile in seven CI shards, materialize 3352 Linux preprocessed inputs and report fail-closed compile coverage. Does not claim assembly object or Linux Image boot.
- Both are configured to run once on their own YAML addition, then manual dispatch only, to avoid automatic heavy CI proliferation. Their scoped `push.paths` triggers do not include the general CI/script locations.

## Exit criteria before changing the real runtime profile

1. Both integration gates green at the **same runtime source SHA**, with complete profile and binary SHA256 evidence.
2. Check old production profile vs new candidate at matched inputs and hardware, separating performance from semantic/codegen differences. Do not require bit-identical assembly when correcting a known semantic bug; instead prove semantic and ABI correctness.
3. Rebuild changed Linux objects and re-certify link/QEMU/early-boot milestones using cache keys incorporating the candidate compiler identity. Do not use the old Image cache as proof for new MiniC output.
4. Promote only the proven changes to the actual canonical runtime script; then converge and retire the now-redundant performance branch. Avoid direct whole-branch merge (it would resurrect multiple experimental workflows).
