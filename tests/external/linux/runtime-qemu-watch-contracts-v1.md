# Linux runtime QEMU watcher contracts V1

`.github/workflows/linux-runtime-qemu-watch-contracts-v1.yml` consolidates the two independent certification contracts for `tools/ci/linux-runtime-qemu-watch-v1.py` without changing either job body.

It preserves:

- `qemu-watch-cert`: compare the event-driven watcher with the fixed 8-second reference oracle on the certified runtime fixture, require exact verdict/fault/progress equivalence, require `SAME_FAULT`, and require an oracle-driven stop before the hard timeout.
- `inconclusive-cert`: first probe the exact frozen early image to discover its observed `highest_progress`, then synthesize a baseline at that observed stage with only the fault identity replaced by an unknown fault. The second watcher run must return final verdict `INCONCLUSIVE`, must stop as `oracle:INCONCLUSIVE:fault` once the unknown concrete fault is observed, must stop before the hard timeout, and must return the inconclusive classifier code (`2`). This matches the current watcher policy in `probe_decisive()`: an inconclusive result with a concrete fault is terminal enough to stop without inventing an ordering conclusion.

Legacy triggers remain accepted by their corresponding jobs:

- `[linux-runtime-qemu-watch-cert-v1]`
- `[linux-runtime-qemu-watch-inconclusive-v1]`

The canonical trigger `[linux-runtime-qemu-watch-contracts-v1]` runs both jobs on the same HEAD.

Canonical certification run `37469068307` on `agent/linux-expanded-kbuild-v0` completed SUCCESS with both consolidated jobs at the same HEAD. `qemu-watch-cert` passed exact reference/watcher verdict, fault, progress, and early-stop equivalence. `inconclusive-cert` passed the fault-aware same-stage unknown-fault contract: final verdict `INCONCLUSIVE`, stop reason `oracle:INCONCLUSIVE:fault`, early stop before the hard timeout, and classifier return code `2`. Therefore the two historical standalone watcher workflows are superseded and may be archived without deleting their YAML history.


## Historical inconclusive-policy retirement

The old standalone `linux-runtime-qemu-watch-inconclusive-v1.yml` required every same-stage unknown fault to run until `hard-timeout`. That expectation predates the current fault-aware watcher policy. The current watcher deliberately treats `INCONCLUSIVE` with a concrete observed fault as terminal enough to stop early while still returning the inconclusive classifier code and never converting it into `REGRESSED` or `MOVED_LATER`.

The standalone hard-timeout policy is therefore obsolete rather than a stronger safety gate. Its YAML is retained in history and may be archived once the consolidated current-policy contracts certify successfully.
