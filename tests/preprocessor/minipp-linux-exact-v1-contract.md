# MiniPP Linux Exact V1 contract

`MiniPP Linux Exact V1` is the canonical frozen Linux 6.6.143 exact-preprocessing gate.

It covers the complete 3352-entry frozen contract as six non-overlapping shards:

- 0-499
- 500-999
- 1000-1499
- 1500-1999
- 2000-2999
- 3000-3351

Each shard verifies the frozen contract identity, restores or builds the matching GCC reference corpus, checks corpus identity, replays its focused sample, and then replays the complete product shard with the current MiniPP.

The historical per-range workflow wrappers are retained under `.github/workflows-disabled/`; the canonical matrix replaces only those wrappers. The live-Kbuild `exact-72`, `exact-batch`, and `exact-smoke` diagnostics remain separate because they exercise a different execution path.
