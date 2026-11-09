# MiniC compiler-throughput recovery: DataLayout import batching (2026-11-09)

**Decision: keep / performance gate passed on eligible Linux owners; runtime acceptance not yet full.**

## Scope and cause

The existing source-policy DataLayout implementation was largely unchanged by the
refactor. The new C-to-Core type importer instead called a complete-record
field-placement walk once **for every field**, and separately recalculated each
field's storage alignment. This is compiler execution overhead, not target-code
quality. The one-factor patch adds a transient, caller-owned field layout
snapshot and reuses the canonical DataLayout validator; it changes no frontend,
Core IR or Machine IR representation and adds no mutable-program cache.

- Production change: [be6351e](https://github.com/yituanxing/minic-toolchain/commit/be6351e372af411c750f355d6af9457bb7471760).
- Old exact refactor baseline: `dd08e4a83abfcb1b286d17edae297afa70319021`.
- Focused regression: `make check-layout`, scalar-vs-batch field offsets,
  bit offsets, storage alignment and rejected bad cardinality.
- Same parent vs candidate output: SHA-identical assembly for every accepted
  target in the fixed samples.

## Verified performance evidence

| Evidence | Before | After | Outcome |
| --- | ---: | ---: | --- |
| Genuine GNU Linux 6.6.143 `fs/ext4/fsync.o`, warmup plus 4/arm balanced paired P50 | 474.227 ms | 435.457 ms | **1.0890x**; SHA-identical `.s` |
| Same `fsync.i`, Callgrind simulated host instruction count | 2,344,410,500 Ir | 2,110,371,144 Ir | **9.98% less** |
| Same Runner, fixed first500: 493 mutually accepted and SHA-identical TUs | 241,513.256 ms | 227,880.457 ms | **1.05982x**, 5.645% less wall |
| CPU time on the same 493 owners | — | — | **1.05986x** |
| Among 493 comparable owners | — | — | 484 faster, 9 slower |
| New rejected TUs / differing assembly | — | — | **0 / 0** |

- Focused [CI #42556155749](https://github.com/yituanxing/minic-toolchain/actions/runs/42556155749).
- Authenticated Linux first500 [CI #42557348539](https://github.com/yituanxing/minic-toolchain/actions/runs/42557348539), artifact `13647854494`, SHA-256 `acc9e71e885197d3ecd134412efefeee28df6bc5e62d5b058049fc2764763e24`.
- Corpus: 500 Kbuild-produced `.i` files with 500/500 SHA validation;
  `inputs.sha256` manifest digest `98d395d1c655e955fa8848274ff95e640436ba0ab5cad2d2d4dc86d4cf7524de`.
- Comparator: parent/candidate release `minic-cc` binaries, each owner
  compiled once per arm in stable balanced order; time excludes GNU -E,
  toolchain linking and QEMU. Never label a Callgrind instrumented run as
  real-time speed.

**Qualification:** Exactly 493/500 owners are comparable; **full Linux
500/500 acceptance is NOT proved**. Four owners fail in BOTH compiler versions
with the same indirect-call argument type mismatch:
`init/main.o`, `arch/riscv/kernel/setup.o`,
`arch/riscv/kernel/efi.o`, `arch/riscv/kernel/acpi.o`.
Another three exit with status 0 in BOTH versions but do not produce a
nonempty `.s`: `mm/shmem_quota.o`, `mm/highmem.o`,
`fs/iomap/trace.o`. These three were originally bucketed with dual failures;
the audit script has since been corrected to call them `both_no_assembly`.
No claimed performance includes these seven.

### Subsequent Linux replay ABI audit

The first500 numbers above describe the original **bare `-S` replay**, not a
faithful reproduction of GNU Kbuild's invocation-scoped C language ABI.
Linux 6.6.143 sets `-fshort-wchar -funsigned-char` in `KBUILD_CFLAGS`, but the
first500 and frozen-differential drivers had not forwarded them to MiniC.
The existing EFI indirect-call negative test deliberately reproduces the
same diagnostic under normal 32-bit `wchar_t`; the four dual failures must
therefore be **reclassified after a fresh paired run with both Kbuild flags**,
not treated as established compiler correctness bugs. The three rc0/no-asm
units also require separate original-GNU output-root analysis.

The paired harness now records `kbuild_language_flags` and forwards the
known frozen RV64 C ABI in both arms. The 2064 Kbuild wrapper instead forwards
the actual target/language flags captured from each compiler invocation.
Neither mechanism proves all other per-owner original code-generation
options were replayed; future campaigns should retain complete original
Kbuild command provenance. Do not reuse the 493-owner 1.05982x figure as a
new ABI-corrected benchmark result.

## Historical decision reuse and next gate

Historical `rv64-record-layout-single-walk-v1` optimized a DIFFERENT
`riscv64/layout.c` caller. Its four-TU 1.01998x result was
`NOT_PROMOTED_STOP`; it is not the new C-to-Core importer snapshot.
The current importer-specific change passes the predeclared 1.04x gate even
across all **eligible** frozen first500 inputs.

Do not redo previously rejected tiny Lexer, Parser enum, RV64 layout, or Core
source-bucket micro-optimizations. Next: restore the remaining four actual
indirect-call C-to-Core/type ABI correctness failures, adjudicate the three
zero-output cases, and then reattempt a **complete** Linux acceptance gate
before any 2064/QEMU claim. Five-target target-code quality optimization
remains deliberately deferred.
