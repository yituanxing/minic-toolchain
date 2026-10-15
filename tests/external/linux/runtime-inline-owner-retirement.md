# Historical runtime inline-owner probe retirement

The active `.github/workflows/linux-runtime-inline-owner-v0.yml` workflow is retired as an incomplete one-off historical localization probe.

It is hard-coded to the September specialization symbol `__minic_inline_spec_720_39`. It does not run a runtime oracle, does not accept a generic symbol input, and does not define a reusable regression contract.

Repository history shows one workflow-introduction commit, `a53bf512d94970336235e4ae3bbf9af23ec90568` ("ci: locate current inline runtime frontier owner"). Its only recorded Actions run, `36406037865`, was CANCELLED and therefore never became a certified diagnostic gate.

Current runtime owner localization is covered by maintained current-frontier diagnostics such as linked `get_current` owner analysis, timer/RCU first-fault owner scans, fault-context analysis, and QEMU frontier workflows.

The historical YAML is preserved byte-for-byte under `.github/workflows-disabled/`; no successful certification result is claimed for this retired probe.
