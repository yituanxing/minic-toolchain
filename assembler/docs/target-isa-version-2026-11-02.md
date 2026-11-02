# MiniAS Target ISA versions and ABI validation (2026-11-02)

This batch continues the immutable `MiniAsTargetSpec`/object-ABI migration.
It is an internal Target-owned data and parsing change, **not** a claim that
arbitrary RISC-V extension revisions or new ISA families can be encoded.

## One file ISA, separate lexical permissions

- `MiniAsRiscvProfile.isa` and the Target state's `file_isa` carry one typed
  `MiniAsRiscvISA`: XLEN, I/E base and standardized version, a feature set,
  and per-extension version values. The 11 supported feature entries now
  supply their own version number to parser recognition, file attributes
  and source-site mapping projection; the fixed `attribute_suffix` table and
  the duplicate Target-state `isa_features` field are gone.
- Existing unversioned `-march=rv32/rv64...` inputs retain their exact old
  normalized ELF spelling; explicitly spelling **the same default versions**
  produces byte-identical objects. Other revisions still fail closed. For
  example, `rv64i2p1m2p0_zbb1p0` works, but a requested `zbb1p1` does not.
- The separately mutable `.option` push/pop stack and per-Fragment Site
  snapshots are unchanged. The two already supported local extensions now
  also recognize `+zbb1p0` and `+v1p0`; those are lexical aliases of
  `+zbb`/`+v`, not newly writable file features. A local `+v` retains its
  GNU 2.42 F2p0/D2p0/vector-dependency mapping projection; file-level
  F/D defaults remain 2p2. `.option norvc` retains its existing instruction,
  alignment and mapping behavior. In particular, only file ISA drives the
  serialized Tag_RISCV_arch.
- The other three Targets continue to resolve their ISA and object ABI into
  the same immutable target spec. x86 `-march=i386` now requires i386 object
  ABI, while ABI-only `-mabi=i386` still selects a valid ELF32 object.
  Incompatible `-march=i386 -mabi=sysv` and implicit ELF64 combinations
  are rejected before assembly.

## Regression and external oracles

The new `probe-rv-versioned-target-profile.sh` tests nine explicit/default
alias pairs for **whole-object byte equality**, all nine against GNU 2.42
Tag_RISCV_arch, a source-local Zbb+V versioned alias against GNU's `.text`
and `.riscv.attributes`, and eleven invalid ISA/option cases. The existing
GNU RV32/RV64 V-site test now rejects unsupported V1p1 while covering V1p0
through the new positive test. `probe-x86-i386-object-rel-gnu.sh` checks
three incompatible x86 profile combinations and a valid ABI-only i386 ELF32.
Both regression paths run in the exact-head Target workflow.

Before publication: release `-Werror`, A0 30/30 with local RISC-V GNU
cross assembler and pyelftools, 22 independent before/after four-target
fixtures (15 identical objects, 7 identical refusals), TargetSpec const
gate and RISC-V canonical target/attribute semantic tests all passed.
The baseline object comparator uses the older immutable Section-owner
binary; it proves byte equivalence of these unchanged fixtures, not full
Linux-image equivalence. Remote exact-head and Linux QEMU outcomes must be
reported only after observing them separately.

## Explicit limits and next dependency

No new extension whose encoding/alignment/relaxation was not verified is
enabled by these version aliases. Additional versions, `.option arch,+c`
and other source-local additions must update the encoder, alignment,
Fragment Site/replay, mapping and ELF behavior **together**, with GNU and
real Linux input evidence. GNU's final `relax` state for deferred ULEB
relocations is deliberately unchanged. Once this ISA contract is stable,
the next structural batch should audit the existing terminal ObjectModel
view for genuinely format-neutral entities; do not build a fake generic
COFF/Mach-O writer without an executable format consumer and oracle.
