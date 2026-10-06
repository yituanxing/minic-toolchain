# Linux runtime compiler semantics V1 contract

`.github/workflows/linux-runtime-compiler-semantics-v1.yml` consolidates the two lightweight compiler-level runtime probes without changing their job bodies.

It preserves:

- `pi-local-symbol`: certified compact/frozen fixture restore, exact runtime MiniC profile, PI address-lowering semantic check, current frontier-owner replay, compact early relink, and QEMU frontier execution. `SAME_FAULT`, `MOVED_LATER`, and `FRONTIER_PASS` are accepted as non-regressing outcomes; `REGRESSED` or an unclassified result fails the probe. The legacy trigger `[linux-runtime-pi-local-symbol-v0]` remains accepted.
- `satp-micro`: exact runtime MiniC profile plus the focused stack-free SATP transition micro test. The legacy trigger `[linux-pi-micro]` remains accepted.

The canonical trigger `[runtime-compiler-semantics-v1]` runs both jobs together.

Because the PI-local-symbol job consumes GitHub Actions caches scoped to `agent/linux-expanded-kbuild-v0`, the canonical workflow is intentionally scoped to that runtime branch. The two historical standalone workflow files remain active until both jobs are independently green in one canonical run.
