# MiniC four-backend architecture (2026-10)

This document describes the **implemented** MiniC interfaces on the
`feature/four-backend-aarch64-loongarch64-m0` integration branch. It is not
a promise of a universal Machine IR, LLVM-style allocator, or a rewrite of
MiniAS/MiniLD/MiniPP. The current milestone ends when ownership and regression
behavior are stable; performance improvements can follow independently.

## End-to-end ownership

```text
C source
  → Frontend (parser, C types, AST, initializers)
  → Core IR (typed values, objects, blocks, memory/flow semantics)
  → target admission and function planning
  → target lowering / instruction-family selection
  → target machine emission (assembler text)
  → external assembler, linker, real ELF runtime
```

The Core instruction kind and type/DataLayout invariants are **semantic**:
they should not be implemented independently in every ISA. Physical registers,
stack calling conventions, varargs layouts, relocation syntax, instruction
encoding and large-immediate legalization remain **target owned**.

Target selection is controlled through the existing MiniC TargetInfo/compiler
driver. No target-specific behavior belongs in MiniAS, MiniLD, or MiniPP merely
because MiniC needs it.

## Actual per-architecture shape

| Concern | x86-64 | RV64 | AArch64 / LoongArch64 |
| --- | --- | --- | --- |
| Admission | Core and local emitter checks | `core_admission.c` | common direct LP64 preflight + machine-family shape |
| ABI | `call_plan.c` SysV 6-GP/8-SSE/x87 | `function_abi_plan.c`, psABI | `direct_call_plan.c` with AAPCS64 / LP64D policy |
| Frame lifetime | `frame.c` | `frame_layout.c`, frame-owned object intervals | `lp64_frame.c`, `direct_function_plan.c` |
| Scalar machine | `machine_emit.c`, `machine_fp.c`, `machine_call.c` | `scalar_emit.c`, `fp_emit.c`, `overflow_emit.c` | target `integer_emit.c`, `fp_emit.c`, `memory_emit.c`, `call_emit.c`, `residual_emit.c` |
| Block / terminator | native machine emitter | `core_codegen.c` | `block_emit.c`: block iteration, family dispatch, GP spill, native terminator |

The two direct LP64 implementations **share the checked function preparation
algorithm** (`direct_function_plan.c`), an instruction-family classifier
(`direct_instruction_semantics.c`), and checked residual operand facts
(`direct_residual_plan.c`). They do **not** share an ABI register bank or
machine instruction emitter. RV64 has its own ABI, object-lifetime model and
inline-assembly behavior, so do not force it into an AAPCS64/LP64D-only API.

### Computations done once

- SysV incoming arguments and outgoing calls receive a prepared set of
  native locations. Machine emission consumes these locations; variadic
  frame setup uses the same incoming cursor.
- RV64 Core admission runs before function emission, rather than again for
  each emitted instruction. Its named ABI placements move into the frame
  without reclassifying all parameters.
- RV64 object address-use indexes, conservative intervals and stack-slot
  coloring are owned by the frame; no process-global per-function cache or
  cross-block policy switch survives.
- AArch64 and LoongArch64 share instruction-family/result-slot checks and
  integer-cast widths/signs/128-bit Core stack-slot facts; native machine
  lowering consumes those facts.
- Each direct LP64 ISA has one target-owned `scalar_normalize.c` primitive
  reused directly by FP, integer, ABI and residual machine families. No
  block-emitter callback or forwarding parameter remains across modules
  inside the same ISA. This is independent of the Core type-width classifier.

### Failure and lifetime invariants

- A rejected Core instruction must fail closed with the diagnostic context,
  never silently emit an arbitrary register value.
- Every dynamically allocated per-function plan has one cleanup owner.
  Transfer of named RV64 ABI locations to the frame is an explicit move.
- An effect-only Core instruction is not required to produce a scalar SSA slot.
  Wide 128-bit values require their dedicated 16-byte frame placement.
- The raw stack pointer and the stable frame anchor differ under dynamic
  alloca. AArch64 and LoongArch64 preserve their own concrete lowering.
- Never merge unrelated ISA-specific register, varargs, stack, GOT and ELF
  relocation policies under the name of a generic ABI.
- A refactor is not done until its old duplicate calculation/ownership path
  is removed; moving the same function to a smaller file does not qualify.

## Proposed fifth-architecture interface

This is a **review checklist**, not a requirement to implement Machine IR:

1. Implement TargetInfo/DataLayout and prove integer widths, endianness,
   aggregates, alignment, and record padding against an external toolchain.
2. Implement the target call/return ABI classifier and named/outgoing argument
   planners. Reuse Core call views/semantic facts, not another ISA's call
   register assignment.
3. Build a function-owned frame and stack-state plan, plus admission for the
   exact set of Core instructions the new machine emitter supports.
4. Dispatch verified Core instruction families to native emitters. Leave
   machine immediates, scratch registers, signed extension, and relocation
   syntax to the new target.
5. Keep the terminator path and function prologue/epilogue target owned.
   Where large-offset legalization is needed, make it explicit and tested.
6. Compare original third-party C source builds against GNU/Clang output,
   then link and run on hardware or QEMU. Only expand the frontier after the
   existing validated cases continue to pass.

If a future Machine IR demonstrably avoids meaningful duplication across all
four existing emitters, it can be added between function planning and target
instruction emission. The current code deliberately exposes that seam without
enforcing a premature, universal physical register/selection model.

## Regression contract and ownership checks

Run `bash tests/compiler/c0/check-minic-target-boundaries.sh` after changing
machine family ownership. It rejects regressions such as embedding the LP64
residual switch back into both block emitters, resurrecting per-function static
RV64 object caches, or reintroducing a native normalize callback between files
of the same target.

The apt-free `MiniC Native Strict Refactor Gate` executes that checker, then
builds with `-Werror` and exercises native x86 ABI/runtime behavior. The
focused regression suite separately exercises original source programs,
cross-toolchain linking and the RV64/AArch64/LoongArch64 GNU/QEMU oracles.
A green native gate does **not** imply the full cross-target suite passed.

Scope: **MiniC only**. A failing test in MiniAS/MiniLD may still be informative,
but the backend-architecture refactor does not modify those toolchains.

## GNU inline assembly and ABI cursor extension contract (2026-10-23)

The driver still selects one of four `MinicBackend` implementations; the
Core-to-Backend machine emission seam is unchanged. New GNU multi-operand
assembly forms now qualify through the selected backend's optional
`qualify_inline_asm` callback. x86 owns GNU register constraints, instruction
whitelists and template qualification in `x86_64/asm_contract.c`; the Core
`core_lower_asm_target.c` only turns target-approved operand roles into typed
SSA values/addresses and a structured side-effect instruction. The x86
`machine_asm.c` continues to own physical registers, SysV preservation,
clobbers and assembly output. Both qualification and emission use the same
x86 template predicates, preventing the two rule sets from drifting.
Other ISAs retain their existing ordered Core GNU asm probes until a proven
new contract is supplied. Unsupported forms must not mutate Core IR.

`va_copy` follows `TargetInfo.variadic_copy_kind`: scalar cursor assignment,
ABI state copy, or explicit unsupported. The Core ABI-state sequence allocates
independent cursor storage, copies its semantic state, then writes back the
cursor address. The target checks supported cursor types; x86 still owns its
24-byte SysV tag copy and frame layout. The transitional scalar policy of the
other three ISAs is preserved, not upgraded to full structured ABI support.

The mandatory backend-boundary gate now rejects restoration of x86 Core
architecture dispatch and ensures that template classification stays owned
by the ISA. Extend the architecture's existing qualifier/emitter rather than
adding another `core_asm_try_<ISA>` branch for each new Linux inline asm.

## Target language admission ownership (five-backend pass)

TargetInfo now owns the declaration-time type admission decisions previously
duplicated by architecture allowlists in `parser_type.c` and `ast_verifier.c`.
The frontend uses the same query for extended floating point identities, and
TargetInfo owns header-only opaque vector names plus the builtin va_list cursor
identity. RV32 continues to reject the extended floating declarations; x86-64
retains its ABI-state cursor, and AArch64 alone recognizes its opaque NEON/SVE
header spellings. No ABI marshaling, storage layout or arithmetic support is
implied by accepting a declaration. The existing emission and Core lowering
paths are intentionally unchanged.

The boundary gate rejects new `MINIC_TARGET_ARCH_` allowlists in the frontend.
The native strict gate additionally runs the five-target descriptor contract.
This is the first semantic-ownership pass; the RV32/RV64 machine legalization
and product-driver predefined-macro unification remain separate follow-ups.
