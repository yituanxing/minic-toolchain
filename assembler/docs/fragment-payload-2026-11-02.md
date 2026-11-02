# MiniAS — FragmentKind-discriminated payload ownership (2026-11-02)

This batch removes the flat, mutually exclusive fields of `MiniAsStmt` in
`minias_target_api.h`. The record continues to carry its stable SourceSite,
Section/Subsection, source spelling and final offset/size. The **one**
`MiniAsFragmentPayload` union has exactly one live arm selected by the
existing `MiniAsFragmentKind`:

| FragmentKind | Union arm | Semantic owner |
| --- | --- | --- |
| `INSTRUCTION` | `instruction.inst` | Target parser constructs MCInst; Common destroys |
| `ALIGNMENT` | `alignment` | Lowering captures alignment, explicit fill/max-skip; MCLayout computes padding; Target may encode NOPs |
| `ORIGIN` | `origin` | Lowering captures absolute/dot/symbol/typed expression, own fill; MCLayout resolves `.org` |
| `FIXED_DATA` | `fixed_data.data` | Shared MCExpr compiles one retained MCData; Emission emits its fixups |
| `FILL`, `SPACE` | `variable` | Shared retained expressions and layout-resolved repeat/width/value |
| `BYTES` | `bytes` | String/incbin/literal LEB snapshots; symbolic LEB has its own retained expression and deferred width |

There is **no compatibility mirror** such as `stmt->mcinst`, `stmt->mcdata`,
`stmt->static_data`, `stmt->org_mode` or `stmt->variable_exprs`, and no extra
data-owner object that requires synchronization with another graph. A failed
partial parse is destroyed by **switching on the kind**, so only the active
arm's pointers may be freed. The single union also distinguishes `.org` fill
from alignment fill; these are not a shared writable field anymore.

All consumers, including SourceGraph forced-branch replay, the four target
parsers/encoders, Common MCExpr, MCLayout and Emission, read their kind's arm.
In particular, Target pre-layout callbacks borrow the entire Fragment list:
**they must test `fragment_kind` before looking at an instruction pointer**.
The LoongArch section-relaxation discovery loop now does so. The old flat
layout made its non-instruction pointer check appear safe, but the union
exposed the invalid read when a `.space` field overlapped the MCInst pointer.
SourceGraph similarly checks the kind before accessing the instruction arm.

The deferred-data fixed point dispatches by kind before accessing the bytes
versus variable-size expression state. Fixed-width MCData compilation and
emission reject the wrong FragmentKind before touching their arm. This batch
does not change ISA encodings, SectionKey/SymbolID identity, Fragment growth,
transactional Fixup commit, ELF serialization or GNU source replay.

## Verification

- `probe-mc-fragment-payload-union.sh` checks all seven kind mappings, rejects
  22 removed flat fields with a negative C compilation, verifies active-arm
  destruction, and guards LoongArch pre-layout/SourceGraph access order. It
  runs in the MiniAS Target ISA/ABI workflow.
- The existing `probe-mc-layout-ownership.sh` checks emission of the cached
  `payload.variable.resolved` representation, not the old flat field.
- `MODE=release CFLAGS=-Werror check-minias-a0` and deferred-shift GNU oracle
  cover `.space`, `.fill`, `.org`, symbolic LEB and all four targets. The
  deferred-shift oracle also ran under ASan/UBSan in this batch.
- Full opt-in 3,536 GNU comparisons and Linux/QEMU P1 remain independent
  acceptance gates; a local A0 pass does not imply them.

See [P06 Common owners](common-owner-split-2026-11-02.md) for the opaque
allocation domains and [the current cleanup map](current-cleanup-map-2026-11-01.md)
for the next gates.
