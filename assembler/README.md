# MiniAS assembler component

Ownership: target assembly source (`.s`) to ELF relocatable object (`.o`),
currently covering RISC-V (RV32/RV64), AArch64, x86-64 and LoongArch64.

Public tool identity: `minic-as`.

## Architecture

MiniAS deliberately does not build a compiler-style AST for an entire `.s`.
It does retain typed instruction/operand objects, expression trees and MC
fragments for layout, fixups and output:

```text
assembly text
  -> SourceGraph / macro and conditional expansion
  -> common statement/directive parser + target instruction parser
  -> typed MCInst / MCExpr / sections / symbols / fragments
  -> MCLayout / target relaxation / encoding / fixups
  -> ELF32 or ELF64 ET_REL writer
```

See [the target boundary migration contract](docs/target-boundary.md) for
implemented ISA/ABI behavior and remaining architecture work.
See also [the 2026-10-31 architecture discussion decisions](docs/architecture-discussion-decisions-2026-10-31.md)
for the agreed ISA/ABI/configuration and Parser-to-MC design boundaries,
verified source gaps, and proposed follow-up order (not implementation status).

The C implementation is split by ownership:

- `source_graph.c`, `source_expansion.c`: source-site identity and GNU source expansion.
- `assembler.c`: common statements, sections, symbols and assembly orchestration.
- `mc_layout.c`, `mc_expr.c`: retained layout and expression semantics.
- `riscv_encode.c`, `aarch64_target.c`, `x86_64_target.c`, `loongarch64_target.c`:
  target-specific instruction parsing, measuring and encoding.
- `elf_writer.c`: relocatable ELF object construction through `minielf`.
- `tools/minic-as/main.c`: standalone command-line driver.

The older Python MiniAS is used as a semantic and bug-history reference, not as a
physical code template.

## Explicit strict RV64I profile

MiniAS historically accepts a broad set of RISC-V instructions and GNU
pseudos. Merely supplying `-march=rv64i` previously changed the ELF flags
but did not restrict encoded instructions, allowing an M/F/D/A/C opcode to
escape into a purported RV64I object. That was **not** a valid ISA contract.

Now the explicit `minic-as -march=rv64i -mabi=lp64` path validates the
machine words emitted into every executable section *after* pseudo expansion,
alignment and fixup creation. Only RV64I base integer encodings (including
ECALL/EBREAK/FENCE, but excluding the Zihintpause PAUSE hint) are accepted. M/F/D/A/C, CSR, FENCE.I and unqualified
instructions fail with an encoding/section offset diagnostic and do not
produce a successful .o. Raw .insn, data in executable sections, .org in
executable sections, and ISA-altering/ignored .option directives fail
closed, rather than concealing unverified code bytes.

This is deliberately an opt-in policy of the *assembler*, independent
from MiniC's Core/ISA selector. The historical default and other -march
spellings retain their prior general Linux assembly compatibility until
they receive explicit ISA-mode qualification. Explicit RV64I now emits `SHT_RISCV_ATTRIBUTES` with
`Tag_RISCV_arch = rv64i2p1` in the exact ELF object, alongside the
LP64 soft-float/no-RVC `e_flags` and byte-level ISA instruction gate.
Legacy/default assembler input remains unchanged.
A generic Linux ELF with arbitrary inline data in .text is **not**
supported by the strict-I opt-in mode until such data is separately
represented with a provable instruction/data distinction.

## Real-program convergence

## Linux convergence pipeline

MiniAS convergence is a pipeline, not a single sample loop. The frozen full
window owns the coverage headline; the fast lane continuously works a bounded
active failure sample while the slow full-window lane runs independently.

### 1. Frozen window and authoritative headline

The Linux 6.6.143 corpus is frozen into fixed ordered windows:

```text
0-499       first500
500-999     new500
1000-1499   next500
1500-1999   next500b
2000-2499   next500c
2500-2999   next500d
3000-3351   final352
```

A full-window replay is the only source of the official `PASS / FAIL / ERROR`
headline. Focused or sampled results must never be extrapolated into `X/500`.

### 2. Failure pool and active sample are different objects

Every full-window run produces the complete **failure pool**: every non-PASS TU,
its frozen index/path, first blocker, and failure context.

The fast development lane uses an **active sample of at most 16 TUs** selected
from that pool. When the pool has fewer than 16 entries, the active sample shrinks
naturally (for example sample10 or sample7). When the pool is larger than 16, keep
16 active blocker slots and refill them from the remaining pool as slots become
true PASS.

`assembler/corpus/first500-frontier.txt` records the current active sample for the
first window. It is a moving frontier, not a permanent regression corpus.

### 3. TU state transition rule

A TU leaves the active sample **only when MiniAS produces a valid relocatable
object for that TU**.

```text
same first blocker       -> stays ACTIVE
later/different blocker  -> FRONTIER ADVANCE, stays ACTIVE
internal error/crash     -> ERROR cohort, stays ACTIVE and is prioritized
valid ET_REL object      -> PASS, retire slot and refill from failure pool
```

Moving from one blocker to the next is diagnostic progress, not coverage progress.
For example, removing `csrrw` from a TU that then stops on `.insn` does not turn
that TU green.

### 4. Blocker cohorts and batch repair

Within the active sample, classify by first blocker/root cause. Prefer the largest
coherent blocker cohorts, but independent well-understood cohorts may be repaired
in the same batch instead of forcing one edit/run cycle per TU.

The implementation loop is:

```text
real failure context
  -> exact-shape micro regression
  -> general assembler capability
  -> affected blocker cohort
  -> whole active sample
  -> fixed spread regression sample
```

Do not special-case Linux filenames or individual indices. A fix is promoted as a
general assembler rule and locked by a focused regression.

### 5. PASS-slot retirement and refill

After each active-sample replay:

1. keep every TU that still fails, even if its blocker moved later;
2. remove only true PASS TUs;
3. refill empty slots from the latest full-window failure pool;
4. rerun the refreshed active sample;
5. when the pool is exhausted and the active sample is green, force a new full
   window replay instead of assuming the remaining window is green.

This keeps pressure on new failures without repeatedly paying for all 500 TUs.

### 6. Separate ERROR lane

`unsupported-*`/ordinary capability gaps and `ERROR`/internal/crash failures are
not equivalent. Internal errors or crashes are extracted into a dedicated error
cohort and diagnosed immediately. A window is never promotable with `ERROR != 0`.

### 7. Two concurrent lanes

The fast and slow lanes run concurrently:

```text
FAST: active sample -> cohort fixes -> refill -> active sample -> ...
SLOW: frozen full window -> exact headline/Pareto/failure pool -> next mature head
```

Do not idle the fast lane while a full-window run is executing. Conversely, do not
let sampled success replace the slow lane. Each full run is tied to its exact head;
when it completes, consume its evidence and start the next required full run from
the newest mature head.

The deterministic real16 sample is retained only as a stable spread regression
sentinel. Once it is green, it is not the primary source of frontier work.

### 8. Window promotion

When the current window reaches exact `500/500` (or `352/352` for the final
window), freeze the exact semantic head and evidence and immediately advance to
the next fixed window. Already-converged windows remain regression contracts; a
later capability must not silently regress them.

After all windows converge, run the complete frozen 3352-TU assembly corpus before
claiming Linux assembly coverage complete.

### 9. End-to-end cadence

```text
full window
  -> exact headline + complete failure pool + Pareto
  -> select/refill active sample <= 16
  -> split sample into coherent blocker cohorts
  -> exact-shape tests + batch general fixes
  -> replay active sample
       PASS      -> retire/refill
       ADVANCED  -> keep and peel next blocker
       ERROR     -> dedicated error cohort
  -> real16 regression sentinel
  -> slow full-window refresh on mature head
  -> repeat until exact window is all PASS and ERROR=0
  -> freeze window
  -> next 500 window
  -> ...
  -> final352
  -> all3352 regression
```

GNU `as` remains the differential oracle:

```text
same frozen .s
  -> GNU as  -> reference.o
  -> MiniAS  -> candidate.o
```

Instruction bytes are compared exactly where representation is canonical. Complete
objects are compared semantically by sections, symbols, relocations, contents, flags,
alignment, and other meaningful ELF properties rather than by whole-file SHA.

MiniAS may later share format/ISA facts from `libs/`; it must not call compiler
backend internals to bypass assembly-language semantics.
