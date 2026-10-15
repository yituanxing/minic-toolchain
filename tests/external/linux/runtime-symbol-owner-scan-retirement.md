# Historical runtime symbol-owner scan retirement

The active `.github/workflows/linux-runtime-symbol-owner-scan-v0.yml` workflow is retired as a one-off historical owner-localization probe.

It is hard-coded to the September specialization symbol `__minic_inline_spec_932_103` and does not implement a reusable or current-frontier regression contract. Its only recorded executions were the September 29 localization sequence:

- run `36523342025`
- run `36523584893`
- run `36523726696`

The final run `36523726696` completed SUCCESS. Its scan located the specialization in `vmlinux.o` and preserved the exact disassembly; the emitted specialization used a 1024-byte stack frame.

No later runtime gate depends on this fixed symbol. Current owner/frontier diagnosis is handled by dedicated current-frontier workflows such as the timer/RCU owner chain, linked `get_current` owner diagnosis, fault-context analysis, and focused runtime QEMU oracles.

The historical YAML is preserved byte-for-byte under `.github/workflows-disabled/` so the September experiment remains reproducible from repository history.
