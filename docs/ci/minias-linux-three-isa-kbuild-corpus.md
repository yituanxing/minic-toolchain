# Three ISA Linux 6.6.143 full Kbuild assembler corpus — initial production lane

## Architecture review before change

The previous 70 C-test producer had no real Linux Kbuild provenance. Its
70/46/44 ready assembly counts cannot represent real Linux coverage. This
lane uses exactly one `defconfig` and GNU compiler per architecture, runs
true Linux `make ... vmlinux`, and collects ALL target ELF relocatable `.o`
whose real Kbuild `.cmd` command and GNU `-save-temps=obj` `.s` exist.
The C target files and native preprocessed `.S` are included, and
producer failures / unpaired objects are accounted for separately.

`x86_64`, `arm64`, and `loongarch` Kconfig are different architectures:
"same config" means **one fixed, SHA-verified original config within each
architecture, shared by GNU reference and MiniAS**, not identical `.config`
bytes across three fundamentally different targets. Compiler output is not
forced into a new ISA or a different ABI in this original-config lane.

## Evidence contract

- Source Linux version 6.6.143, archive SHA256
  `dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932`.
- GNU compiler target, version, executable SHA, Kconfig SHA, precise
  Kbuild `.cmd` SHA, original GNU `.o` SHA and frozen `.s` SHA are
  recorded per target/owner.
- Native Linux `.S` files are included only when a real target ELF .o,
  corresponding `.cmd`, and frozen assembler .s exist.
- Producer does not turn `make -k` errors into full Linux success:
  build-exit != 0 means `full_kbuild=false` even if many C/S files exist.
  Unpaired target objects are counted instead of being silently passed.
- Each architecture is partitioned into seven SHA-verified independent tar
  artifacts; MiniAS scans each exactly once, then an aggregator validates 21
  distinct target/shard unions and three separate first-blocker distributions.
- Initial unsupported x86-64 / LoongArch64 targets legitimately produce
  negative capability results; no GNU semantic parity or Linux P1 is claimed.
- Existing frozen RV64 3536 Linux oracle and its separate ISA/ABI regressions
  are unchanged. No new architectural policy in Common/MCInst/ELF.

## Subsequent limited ISA campaign

After the same-config complete original corpus is frozen, separate
`-march` constrained compile outputs should be derived from equivalent
C/ABI inputs, with explicit variant identity. Restricting GCC output does not
erase Linux inline `asm`; never accept missing M/A/Z/CSR instructions in a
"strict-I" object, and do not report a variant as the original Kbuild
reference. Converge GNU/MiniC base subset first, then actual native .S /
inline assembly extension cohorts.

## After-change source boundary audit

Only CI producer/scanner/data contracts and workflow were added. MiniAS
assembler.c, generic Expression/ELF/Fragment and all target sources remain
unchanged. Per-ISA errors are distinct; shared mechanisms may be repaired
only after reviewing their full callers across each target. No legacy
implementation is duplicated in this producer.

## Batch 2: diagnosed real producer and introduced ISA census

- First producer x86-64: 2,605 frozen target C/.S assembly owners
  (2,572 C + 33 native .S), SHA-pinned config
  `5a50dea34d0a1793c6a8320c15f11b695f1c71fe7b4fbb89bc514f2660ddad65`,
  **build_exit=2**; original source/ABI unchanged.
- The full Kbuild log proved 2,046 errors, dominated by GCC 13
  `-Werror=address` diagnostics in DRM/i915 and related files. Next producer
  retries only x86-64 with `-Wno-error=address` (no ISA/ABI relaxation);
  the producer records this in the Kbuild `.cmd` SHA. It is not presented
  as a GNU reference from the *old* unmodified command.
- The seven frozen S shards now independently measure mnemonic and directive
  frequencies; the receiver publishes a separate frequency histogram for each
  ISA alongside first-blocker frequencies. This is a lexical input census,
  not a semantic encoder or GNU object-comparison oracle.
- A new GCC real C→S `rv64i/lp64` contract independently demonstrates the
  desired base-ISA split: GCC-generated C machine instructions stay in I,
  software multiplication calls a runtime helper, and handwritten M inline
  asm remains M and is rejected under strict RV64I.
- Review after change: all work stays in scripts/workflow/tests. Common
  MiniAS ISA dispatch, Section/Symbol, Fragment, Fixup and target encoder
  sources have not changed. Existing per-target code and legacy code remain
  byte-identical. GCC/Kbuild reference and MiniAS are always compared using
  their shared immutable assembly source.

## Batch 3: exact GNU producer cache reuse, not three wasteful rebuilds

The first true Linux build produced **4,140 AArch64 C/.S inputs (4,080+60)**
at config `f865fdfb8ccaa5aa9580e0d5bea7a48c6382d37224f751c6ec37969bbafe174f`
and **2,742 LoongArch64 inputs (2,723+19)** at config
`457d95d9e03620d3f437dfca63bb93c867c30a92649e8705f36ed66d013441ca`.
Both original producer builds passed `vmlinux` and exported complete
seven-way archive families in immutable run `40458021612`. Each receiver
now restores these exact artifacts and verifies every `.s` and `.cmd`
digest and the archived manifest against pinned independent SHA:
AArch64 `e2d034013185b214763c9fa42aef3f3eb4e9d96e276f8678717c1f9d675969cf`,
LoongArch64 `203ed9df2cb4408cd8d0ee48b5de9f2a6444d115e8cdca649cf70995d6fc7017`.

x86 original Linux `defconfig` `5a50dea34d0a1793c6a8320c15f11b695f1c71fe7b4fbb89bc514f2660ddad65`
produced 2,605, then 2,760 partial files. Complete Linux build was blocked
first by 2,046 GCC13 `-Werror=address` warnings and then just one
`-Werror=tautological-compare` in `alternative.c`. The third campaign
changes ONLY diagnostic fatality `KCFLAGS=-save-temps=obj -Wno-error`,
retaining the Kconfig SHA, target ABI/ISA and file-by-file `.cmd`
provenance; this is a new explicit producer variant, not a claim of identical
original compiler command bytes.

The workflow requires the AArch64/LoongArch64 artifact checks before it
runs their seven MiniAS consumer shards. It will fail closed on expired,
unmatched, or missing frozen artifacts rather than silently recompiling.
Census outputs now include per-ISA frequent instructions/directives, and all
21 results remain an incomplete-capability discovery, not P1 or ELF parity.
