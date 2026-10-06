# Linux runtime link fixture certification V1 contract

`.github/workflows/linux-runtime-link-fixture-cert-v1.yml` is the canonical terminal-link fixture certification workflow.

It now certifies two independent equivalence contracts in one run:

1. **Fixture equivalence:** the compact thin-archive fixture linked with `linux-early-runtime-link-v2.sh` must be byte-identical to the certified full fixture linked with the same V2 linker, including `vmlinux.early`, `Image.early`, and the sorted symbol map.
2. **Linker-generation equivalence:** after the immutable V2 full link is captured, the historical `linux-early-runtime-link-v1.sh` is run against the same certified full fixture. Its `vmlinux.early`, `Image.early`, and symbol map must exactly match the already-captured V2 result.

The V1 comparison intentionally runs after V2 because V1 rebuilds the thin `lib/lib.a` archive while V2 proves it can link without mutating that archive.

Canonical certification run `37466316241` on `agent/linux-expanded-kbuild-v0` completed SUCCESS. It independently passed frozen/oracle verification, compact fixture construction, compact V2 link, full V2 link, exact compact/full equivalence, legacy V1 link after V2, exact V2/V1 equivalence, compact runtime frontier classification, cache save, and evidence upload. Therefore `linux-early-runtime-link-shadow-v2.yml` is superseded and may be archived without deleting its YAML history.
