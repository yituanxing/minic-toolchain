# MiniC strict RV64I subtarget: first qualified capability (2026-10-23)

This initial engineering gate deliberately scopes to RV64I + LP64, NOT full
RV64I support for every C construct. The historical default RV64/LP64D
codegen (including kernel Linux 2064/QEMU) is unchanged.

- TargetInfo retains the public compilation context and holds an invocation-
  scoped RISC-V ISA/ABI selection (not a global mode).
- The RISC-V backend receives the whole resolved target instead of dropping
  everything except PIC at its existing single entry boundary.
- Strict RV64I checks the instructions ACTUALLY generated (frame, globals,
  calls, helpers, and inline asm paths), not the source-level C operator. This
  prevents unwanted M/F/D/A/C instructions escaping after optimization.
- The first qualified strict ISA is rv64i; lp64d is explicitly rejected for
  it. Unimplemented rv64im, rv32 and other options fail closed.
- No software multiplication, division, floating point or atomic lowering is
  claimed. If selected machine instructions need these features, report a
  precise compile error, discard the invalid assembly and stop.
- Compiler-side instruction allowlist is a small initial ISA admission table,
  not another code emitter, Machine IR or a full instruction database.
  GNU AS with -march=rv64i -mabi=lp64 remains an independent second oracle.
- Only the strict opt-in mode audits output. All pre-existing four-ISA and
  RV64 Linux source/CI paths are unchanged.

Next: progressively move feature predicates to the RV ISA family planners,
retain the final emission audit as a safety invariant, add qualified software
lowering, and only then support RV32 ILP32 and other ISA configurations.
Preprocessor flags/macros must eventually derive from this same target
configuration when MiniC owns preprocess; the existing .i interface does not
establish such consistency by itself.
