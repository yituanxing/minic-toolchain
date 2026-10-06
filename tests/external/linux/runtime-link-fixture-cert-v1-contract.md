# Linux runtime link fixture certification V1 contract

`.github/workflows/linux-runtime-link-fixture-cert-v1.yml` is the canonical terminal-link fixture certification workflow.

It now certifies two independent equivalence contracts in one run:

1. **Fixture equivalence:** the compact thin-archive fixture linked with `linux-early-runtime-link-v2.sh` must be byte-identical to the certified full fixture linked with the same V2 linker, including `vmlinux.early`, `Image.early`, and the sorted symbol map.
2. **Linker-generation equivalence:** after the immutable V2 full link is captured, the historical `linux-early-runtime-link-v1.sh` is run against the same certified full fixture. Its `vmlinux.early`, `Image.early`, and symbol map must exactly match the already-captured V2 result.

The V1 comparison intentionally runs after V2 because V1 rebuilds the thin `lib/lib.a` archive while V2 proves it can link without mutating that archive.

The historical `linux-early-runtime-link-shadow-v2.yml` remains active until this augmented fixture certification completes successfully in the canonical `agent/linux-expanded-kbuild-v0` cache scope.
