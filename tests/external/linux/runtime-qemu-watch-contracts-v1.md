# Linux runtime QEMU watcher contracts V1

`.github/workflows/linux-runtime-qemu-watch-contracts-v1.yml` consolidates the two independent certification contracts for `tools/ci/linux-runtime-qemu-watch-v1.py` without changing either job body.

It preserves:

- `qemu-watch-cert`: compare the event-driven watcher with the fixed 8-second reference oracle on the certified runtime fixture, require exact verdict/fault/progress equivalence, require `SAME_FAULT`, and require an oracle-driven stop before the hard timeout.
- `inconclusive-cert`: first probe the exact frozen early image to discover its observed `highest_progress`, then synthesize a baseline at that observed stage with only the fault identity replaced by an unknown fault. The second watcher run must continue until hard timeout, return final verdict `INCONCLUSIVE`, and use the inconclusive process return code. This isolates watcher conservatism from repository-wide frontier movement and avoids hard-coding any historical or current stage.

Legacy triggers remain accepted by their corresponding jobs:

- `[linux-runtime-qemu-watch-cert-v1]`
- `[linux-runtime-qemu-watch-inconclusive-v1]`

The canonical trigger `[linux-runtime-qemu-watch-contracts-v1]` runs both jobs on the same HEAD.

The historical standalone workflows remain active until both consolidated jobs complete successfully on the canonical `agent/linux-expanded-kbuild-v0` runtime branch.
