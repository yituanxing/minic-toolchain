# MiniAS focused diagnostics V1 contract

`.github/workflows/minias-a0-focused-diagnostics-v1.yml` consolidates the three historical first500-focused diagnostic workflows without changing their diagnostic workloads.

It preserves three modes:

- `real16`: regenerate a 16-file real-Linux sample, assemble it, and retain blocker context.
- `frontier`: map `assembler/corpus/first500-frontier.txt` into the frozen first500 corpus, replay the current frontier, and retain blocker context.
- `vector33`: replay frozen first500 index 33 (`arch/riscv/kernel/vector.i`) and print its focused failure context.

Manual dispatch selects exactly one mode. Historical commit triggers `[minias-real16]`, `[minias-frontier]`, and `[minias-vector33]` remain supported, so existing debugging habits do not lose an entry point.

The workflow is diagnostic rather than the canonical hard acceptance gate. After all three modes are independently green on the cleanup branch, the three dedicated historical workflow files may be archived without deleting their YAML history.
