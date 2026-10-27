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
