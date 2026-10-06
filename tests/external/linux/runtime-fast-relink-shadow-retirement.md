# Fast relink shadow retirement contract

This cleanup retires the historical `linux-fast-relink-shadow-v1.yml` workflow after its equivalence question was promoted into the canonical runtime link-fixture certification.

## Historical shadow

The workflow was introduced at commit `0791905535ae51119331b55ec66ff47b9578d82c` to validate the terminal-stage fast relink path against the then-certified full linked Linux fixture.

GitHub Actions run `35486990076` completed SUCCESS and established:

- the certified reference classified as `FRONTIER_VERDICT=SAME_FAULT`;
- the fast relink completed with `FAST_RELINK=PASS`;
- the fast-relinked image classified with the same frontier;
- `FAST_RELINK_ORACLE=PASS binary=exact runtime=exact verdict=SAME_FAULT`.

That workflow was a shadow experiment: it proved that the alternate relink implementation reproduced the certified fixture and runtime behavior.

## Canonical fixture/link certification

The maintained `linux-runtime-link-fixture-cert-v1.yml` workflow now owns a stronger terminal-link equivalence contract.

Canonical run `37466316241` completed SUCCESS and independently proved:

- frozen fixture/oracle verification;
- compact thin-archive fixture construction;
- compact V2 early link;
- full-fixture V2 early link;
- `COMPACT_LINK_BINARY_EQUIVALENCE=PASS`;
- legacy V1 link against the same full fixture;
- `EARLY_LINK_V2_LEGACY_V1_EQUIVALENCE=PASS`;
- compact runtime frontier classification;
- `COMPACT_LINK_RUNTIME_ORACLE=PASS`.

It therefore covers both binary equivalence and runtime equivalence while also certifying the compact fixture used by the current fast runtime workflows.

## Retirement decision

The historical workflow is preserved byte-for-byte as:

`.github/workflows-disabled/linux-fast-relink-shadow-v1.yml`

and removed from the active Actions set.

The canonical `linux-runtime-link-fixture-cert-v1.yml` and `linux-runtime-frontier-fast-v5.yml` remain active.

No historical YAML, commits, workflow runs, caches, or artifacts are deleted.
