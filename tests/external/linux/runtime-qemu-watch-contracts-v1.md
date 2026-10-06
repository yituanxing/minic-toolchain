# Linux runtime QEMU watcher contracts V1

`.github/workflows/linux-runtime-qemu-watch-contracts-v1.yml` consolidates the two independent certification contracts for `tools/ci/linux-runtime-qemu-watch-v1.py` without changing either job body.

It preserves:

- `qemu-watch-cert`: compare the event-driven watcher with the fixed 8-second reference oracle on the certified runtime fixture, require exact verdict/fault/progress equivalence, require `SAME_FAULT`, and require an oracle-driven stop before the hard timeout.
- `inconclusive-cert`: deliberately replace the baseline with an unknown fault at the same progress rank, require the watcher to continue until hard timeout, require final verdict `INCONCLUSIVE`, and require the watcher process return code used for inconclusive classification.

Legacy triggers remain accepted by their corresponding jobs:

- `[linux-runtime-qemu-watch-cert-v1]`
- `[linux-runtime-qemu-watch-inconclusive-v1]`

The canonical trigger `[linux-runtime-qemu-watch-contracts-v1]` runs both jobs on the same HEAD.

The historical standalone workflows remain active until both consolidated jobs complete successfully on the canonical `agent/linux-expanded-kbuild-v0` runtime branch.
