# Linux runtime compiler semantics V1 contract

`.github/workflows/linux-runtime-compiler-semantics-v1.yml` consolidates the two lightweight compiler-level runtime probes without changing their job bodies.

It preserves:

- `pi-local-symbol`: certified compact/frozen fixture restore, exact runtime MiniC profile, PI address-lowering semantic check, current frontier-owner replay, compact early relink, and QEMU frontier execution. `SAME_FAULT`, `MOVED_LATER`, and `FRONTIER_PASS` are accepted as non-regressing outcomes; `REGRESSED` or an unclassified result fails the probe. The legacy trigger `[linux-runtime-pi-local-symbol-v0]` remains accepted.
- `satp-micro`: exact runtime MiniC profile plus the focused stack-free SATP transition micro test. The legacy trigger `[linux-pi-micro]` remains accepted.

The canonical trigger `[runtime-compiler-semantics-v1]` runs both jobs together.

## Canonical certification

GitHub Actions run `37460113388` executed on `agent/linux-expanded-kbuild-v0` with both jobs enabled at the same HEAD.

- `satp-micro`: SUCCESS, including exact runtime MiniC profile construction and the stack-free SATP transition micro test.
- `pi-local-symbol`: SUCCESS, including certified compact/frozen fixture restore, profile-aware `la/lla` semantic validation, frozen owner replay, compact early relink, and the QEMU non-regression oracle.

The two historical standalone workflows are therefore superseded and may be archived without deleting their YAML history:

- `linux-runtime-pi-local-symbol-v0.yml`
- `linux-pi-satp-micro-v0.yml`

Because the PI-local-symbol job consumes GitHub Actions caches scoped to `agent/linux-expanded-kbuild-v0`, the canonical workflow is intentionally scoped to that runtime branch.
