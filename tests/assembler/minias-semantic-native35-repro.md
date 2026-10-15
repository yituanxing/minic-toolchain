# MiniAS semantic native35 reproducibility

The native35 lane in `.github/workflows/minias-semantic-oracle3536.yml` must not depend on the expired historical `minias-a0-native26-progress` artifact.

The lane reconstructs the certified Linux 6.6.143 RISC-V native35 workload at the current repository HEAD using the same exact targeted Kbuild object-command method as the green canonical `minias-a0-gate-v1.yml` native35 lane. It preprocesses the exact 35 `.S` object commands into deterministic `.s` inputs and retains the Kbuild output directory as the working directory for generated `.incbin` sidecars.

The semantic oracle then compares GNU as and the current MiniAS for all 35 regenerated native inputs. This keeps the semantic-equivalence contract distinct from the hard assembly acceptance gate while removing the expired fixture dependency.

A commit tagged `[minias-semantic-native35]` runs only this repaired native35 lane for focused certification. The full `[minias-semantic-oracle3536]` trigger continues to require C shards, C149, native35, and the aggregate semantic report.
