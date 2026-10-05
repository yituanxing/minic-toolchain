# Linux Core Shards V1 contract

`Linux Core Shards V1` is the canonical frozen-corpus regression gate for MiniC Linux core translation units with global indices 500 through 3351.

## Certified shard map

| Shard | Range | Count | Historical focused pool | Extra corpus rule |
| --- | ---: | ---: | ---: | --- |
| `new500` | 500-999 | 500 | 16 | standard materialization |
| `next500` | 1000-1499 | 500 | 16 | standard materialization |
| `next500b` | 1500-1999 | 500 | 16 | standard materialization |
| `next500c` | 2000-2499 | 500 | 7 | standard materialization |
| `next500d` | 2500-2999 | 500 | 16 | serial materialization + GCC syntax check |
| `final352` | 3000-3351 | 352 | 16 | serial materialization + GCC syntax check |

Every shard must restore or reproduce its historical frozen corpus identity, build the current exact MiniC compiler, replay the historical focused blocker pool, replay the entire frozen shard, classify the strict frontier, summarize progress, and preserve evidence.

The `next500b` historical TU 1573 ASan/UBSan diagnostic remains available as the optional `crash1573` job; it is not part of the normal six-shard certification run.

The former active workflows `linux-core-frontier.yml`, `linux-core-next500.yml`, `linux-core-next500b.yml`, `linux-core-next500c.yml`, `linux-core-next500d.yml`, and `linux-core-final352.yml` are retained verbatim under `.github/workflows-disabled/`. Their frozen ranges, focused blocker sets, syntax/materialization rules, and TU 1573 diagnostic capability are carried by the canonical gate.

This contract does not replace the independent `core-first500-regression`, all-3352, assemble-all-3352, focused-five, slot-reuse, or other distinct Linux core diagnostics.
