# MiniC-only direct LP64 preflight review — 2026-10-21

**Scope:** Only the MiniC compiler (compiler frontend, Core IR and four backend
implementation). MiniAS, MiniLD, MiniPP, shared ELF manipulation, the outer
RV64-only driver, workflows and historical patch scripts are *not* refactored.
They were inspected to establish where MiniC's responsibility ends.

## Review evidence

Repository tree inspection covered the MiniC source owners and all four
backend source files, and the other tool entry points for boundary analysis.
The AArch64 and LoongArch64 direct emitters each embedded byte-equivalent
function admission (29 lines) and CFG terminator admission (66 lines).
Both already share global-object output, LP64 scalar gates, frame shape and
GNU assembly TU cleanup; ABI argument classification and machine emission
must remain target-private. RV64 and x86 keep their independently validated
admission/layout paths.

## One coherent responsibility-level extraction

- New `direct_preflight.{c,h}` owns only those *identical* two LP64 direct
  emitter admission contracts: function eligibility and Core terminator
  support. Both emitters call these before making machine state.
- Preserve the exact input predicates and decision ordering. The original
  four/five individual failure returns become a single caller-owned reject,
  preserving the fail-closed status. Pass the offending block index back.
- Preserve per-architecture census and cJSON CFG trace formats (arch string
  now comes from an explicit parameter). The reported backend source line
  necessarily moves to the caller preflight boundary.
- Preserve original AArch64 AAPCS64 and LoongArch64 LP64D aggregate/variadic
  ABI handling and all frame, ISA instructions, and GNU assembler behavior.
  No new Machine IR or target-independent register allocator.
- Register exactly one new source file in the existing MiniC Makefile.

## Subsequent issues (not edited)

1. Separate later call/operand ABI preflight from frame and emission, but
   only after exhaustive pairwise comparison; subtle FP variadic and
   indirect-record differences are *not* interchangeable.
2. Frontend parser initializer ownership is large; treat static vs runtime
   initialization semantics independently rather than blindly splitting
   parser_* by line count.
3. Core and Inline specialization now use compilation-scoped caches;
   guard this lifetime with multi-TU and negative-OOM regression tests.
4. Outer `minic` driver remains RV64-only, while four-target `minic-cc`
   emits assembly; do not broaden that driver in the present MiniC batch.

## Gate

Require -Werror toolchain compile/link; cJSON and BusyBox cross-target
QEMU/GNU differential; independent original RV64/x86 regressions as
appropriate. Focused CI does not certify 2064-TU Linux throughput.
