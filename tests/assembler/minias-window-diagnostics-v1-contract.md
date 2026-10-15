# MiniAS frozen-window diagnostics V1 contract

`.github/workflows/minias-a0-window.yml` is the canonical frozen-window diagnostic workflow.

It accepts all seven certified frozen Linux C corpus windows:
`first500`, `new500`, `next500`, `next500b`, `next500c`, `next500d`, and `final352`.

For each selected window it verifies the frozen corpus identity and sidecar, builds the current MiniC and MiniAS, runs the MiniAS micro gate, regenerates the full selected assembly window, splits the work into four shards, classifies every assembly result, and emits the diagnostic artifacts `results.tsv`, `failure-pool.tsv`, `error-pool.tsv`, and `candidate-active-sample.txt`.

For `first500` it also preserves the historical `MINIAS_FIRST500_FIRST_PASSES` summary used by the retired dedicated first500 workflow.

The workflow remains a diagnostic progress surface rather than the hard acceptance gate: assembly failures are classified and preserved instead of being converted into a top-level hard failure. The hard fail-closed contract remains `minias-a0-gate-v1.yml`.

After an independent `first500` run succeeds with this workflow, the dedicated `minias-a0-first500.yml` workflow is redundant and may be archived without deleting its YAML history.
