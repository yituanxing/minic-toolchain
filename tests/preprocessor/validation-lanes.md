# Independent MiniPP validation lanes

MiniPP is tested *alone*, with the host/GNU compiler and assembler/linker
unchanged. Exact GCC-E output is the reference contract; a successful parser
smoke test alone is not evidence that GNU preprocessing can be replaced
throughout an entire Linux build.

## Existing current-product gates

- `.github/workflows/minipp-a0.yml` checks the native preprocessor A0
  corpus against GCC. Changes under `tests/preprocessor/**` select this owner.
- `.github/workflows/minipp-linux-frozen-v1.yml` provides independent
  pinned Linux 6.6.143 preprocessing oracles. The explicit push markers
  `[minipp-linux-smoke]`, `[minipp-linux-batch]` and `[minipp-exact]`
  run its separate real-Kbuild smoke, bounded batch and frozen full-shard
  modes; each mode has its own PASS/FAIL result.
- Only a future explicit GNU-baseline **single-MiniPP substitution** and
  complete runtime replay can certify end-to-end native preprocessing.
  The independent MiniC, MiniAS, MiniAR, MiniLD and ELF-tool proof tracks
  must not be conflated with MiniPP's GCC-E textual oracle.

## Acceptance record

For each current-head run, record the exact branch SHA, test mode, examined
input count, reference compiler identity, PASS/FAIL and artifacts. Mark a run
inconclusive if the GCC reference corpus is unavailable, if any source or
configuration identity differs, or if GitHub Actions passes only setup jobs.

This validation does not initiate MiniObjdump implementation, current-MiniC
self-hosting or multi-tool integration.

## 2026-10-11 accepted preliminary current-product evidence

- [MiniPP A0 #38150034665](https://github.com/yituanxing/minic-toolchain/actions/runs/38150034665):
  95/95 byte-identical cases including `include_next`, **SUCCESS**.
- [MiniPP live Linux #38150034676](https://github.com/yituanxing/minic-toolchain/actions/runs/38150034676):
  separate real-Kbuild smoke and product18/batch modes both **SUCCESS**.
  No failure pool was reported for the bounded batch.
- The current opt-in `[minipp-exact]` requests the existing complete frozen
  corpus seven-shard oracle. It is an *additional* independent coverage gate,
  not a substitute for single-MiniPP/Linux Image/QEMU replacement.

Record all pass/fail results at the original tested Git SHA. A frozen-corpus
cache hit is not the same as a new GNU source materialization and must retain
its exact identity checks.
