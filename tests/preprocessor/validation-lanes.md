# Independent MiniPP validation lanes

MiniPP is tested *alone*, with the host/GNU compiler and assembler/linker
unchanged. Exact GCC-E output is the reference contract; a successful parser
smoke test alone is not evidence that GNU preprocessing can be replaced
throughout an entire Linux build.

## Current checkout

- The live baseline workflow is
  `.github/workflows/minic-test-content-audit.yml`. Its `check-minipp-a0`
  target runs the maintained native preprocessor smoke contract.
- The Linux frozen-corpus scripts in this directory are independent/manual
  validation lanes. The dedicated historical workflows named in older
  records are not present in the current checkout, so their markers must not
  be described as active CI gates.
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

## Historical 2026-10-11 evidence

- [MiniPP A0 #38150034665](https://github.com/yituanxing/minic-toolchain/actions/runs/38150034665):
  95/95 byte-identical cases including `include_next`, **SUCCESS**.
- [MiniPP live Linux #38150034676](https://github.com/yituanxing/minic-toolchain/actions/runs/38150034676):
  separate real-Kbuild smoke and product18/batch modes both **SUCCESS**.
  No failure pool was reported for the bounded batch.
- Historical opt-in `[minipp-exact]` requests the existing complete frozen
  corpus seven-shard oracle. It is an *additional* independent coverage gate,
  not a substitute for single-MiniPP/Linux Image/QEMU replacement.

Record all pass/fail results at the original tested Git SHA. A frozen-corpus
cache hit is not the same as a new GNU source materialization and must retain
its exact identity checks.
