# MC-085: `error`/`warning` attributes — diagnostic reachability audit

Status: **OPEN / BLOCKED-DESIGN**. This is a reproducible frontend-to-compiler
semantic gap, but a name-based Core CALL scan is not a sound fix. The original
audit began at `a1ae70fcbaea`; this restored evidence was rebuilt separately
from `6b1c45cf9`. Neither changes production compiler behavior.

## Reproduction

Run after a Release `-Werror` build:

```sh
make MODE=release CFLAGS=-Werror -j8 build/release/bin/minic-cc
sh tests/compiler/audit/audit-mc085-diagnostic-attributes.sh
```

The permanent source file `tests/compiler/audit/mc085_diagnostic_attribute_cases.c`
selects 16 independent TUs with `-DMC085_CASE=N`. The runner feeds GCC O0/O2
the original C source, feeds MiniC the GCC-preprocessed `.i`, and asserts GCC
oracle results. It deliberately **reports**, rather than gates, MiniC's
current unsupported behavior. In the table, `1` means compilation rejected:

| Case | Key behavior | GCC O0 | GCC O2 | MiniC x86 `-S` |
| ---: | --- | ---: | ---: | ---: |
| 1 | direct live `error` | 1 | 1 | 0 (wrong) |
| 2 | `if (0)` direct `error` | 0 | 0 | 0 |
| 3 | folded integer constant condition | 0 | 0 | 0 |
| 4 | direct live `warning` | 0 + warning | 0 + warning | 0, silent (wrong) |
| 5 | attribute target taken via local function pointer | 0 | 1 | 0 |
| 6 | pointer passed through a call, optimizer inlines | 0 | 1 | 0 |
| 7 | direct attributed call with `__asm__("...")` linker name | 1 | 1 | 0 (wrong) |
| 8 | `always_inline guard(0)` with guarded `error` | 1 | 0 | 0 |
| 9 | unused `static` function containing `error` | 1 | 0 | 0 |
| 10 | referenced `static` function containing `error` | 1 | 1 | 0 (wrong) |
| 11 | coexisting `error` and `warning` | 1 + warning | 1 + warning | 0, silent |
| 12 | reachable runtime condition, caller passes zero | 1 | 1 | 0 (wrong) |
| 13 | harmless declaration sharing attributed declaration's linker name | 0 | 0 | 0 |
| 14 | attributed declaration sharing harmless declaration's linker name | 1 | 1 | 0 (wrong) |
| 15 | label inside `if (0)` before `error` call | 0 | 0 | 0 |
| 16 | unreferenced label jumped over before `error` call | 0 | 0 | 0, **emits CALL** |

Both attributes can be present simultaneously (11): GCC prints the error **and**
the warning. MiniC currently prints neither. The MiniC CLI does not offer a C
source `-O0/-O2` option; its optimized *compiler executable* build is not a
corresponding C optimization mode.

## Where facts disappear

1. `src/frontend/ast_function.c:minic_c0_program_set_function_diagnostic`
   owns both messages by **source FunctionId**, including redeclaration
   replacement. `src/frontend/parser_function.c` decodes GNU string literals.
2. `src/core/core_lower_calls.c` selects the callee FunctionId (potentially a
   section-local integer/symbolic/Boolean clone), but serializes only an
   **assembly symbol name and ABI signature** in `MinicCoreCallee`. The Core
   CALL has its source span, not its original declaration identity.
3. `src/compiler/compiler.c` lowers and folds Core literal branches, prunes
   clones, prunes dead file-static globals/functions, verifies Core, then
   emits assembly. There is no diagnostic-attribute pass. The only existing
   compiler read of `error_message` is an inline-specialization admission
   guard, not a CALL diagnostic.

## Three concrete reasons the obvious fix is unsafe

**Linker names are not declaration identities (cases 13/14).** Two distinct C
names `dangerous` (attribute `error`) and `safe` (no attribute) both declare
`__asm__("mc085_same")`. GCC O0/O2 correctly compile `safe()` and reject
`dangerous()`. The lowered Core CALL callee is exactly `mc085_same` in both;
looking up attributed Program functions by Core callee name necessarily either
false-positives case 13 or misses case 14. A canonical symbol table or even
the static-object reachability name resolver cannot repair this provenance
loss.

**Machine-emission CFG roots are not diagnostic CFG roots (case 16).** The
shared `minic_core_function_mark_reachable_blocks` roots **every** Core block
with a `source_label_id`, in addition to the entry block, to avoid losing
possible computed-goto/asm-goto destinations. In
`goto after; cold_label: stop(); after: return 0;`, both GCC modes diagnose
neither error, but MiniC x86 assembly contains `call stop@PLT` in the
otherwise unreachable labeled block. Iterating the existing Core emitted
block set and rejecting every CALL would newly misreject this valid source.
Even the presence of a machine CALL is not a sufficient reachability proof.

**Optimization and indirect calls change diagnostic visibility (5/6/8/9).**
GCC O0 does not complain about `fp()` or a call via `call_through(fp)` when
only the addressed function declaration has `error`; GCC O2 discovers the
direct target and does complain. Conversely `always_inline guard(0)` is an
O0 error but eliminated at GCC O2; MiniC's specialized Core clone already
eliminates the call. The unused-static function similarly triggers the GNU
O0 diagnostic but is eliminated at O2. Taking the address of a function with
the attribute cannot automatically taint every indirect CALL, nor may a
diagnostic be synthesized from a clone's raw source AST before its Core CFG
is resolved.

## Safe implementation contract

The smallest *sound* next implementation must establish these pieces:

1. **Compiler-owned call provenance sidecar.** During source-to-Core lowering,
   attach to each **actually emitted direct CALL** (caller FunctionId, Core
   instruction/block ID) its original source callee FunctionId and source
   expression/span. Preserve it through function-section clone selection and
   avoid leaking `error`/`warning` strings, policy, or invented pseudo-CALLs
   into target-neutral machine instructions. A Core linker name alone cannot
   stand in for this sidecar. Any source declaration aliasing must retain the
   precise source name the user called.
2. **Diagnostic-specific reachability.** Analyze the post-lowering, folded
   CFG from the actual entry, following direct edges. Bring in label targets
   for reachable computed-goto / asm-goto according to their proven or
   conservative target sets, **not every lexical label unconditionally**.
   This is separate from the existing conservative ELF/code-emission roots.
   If a target set cannot be justified, do not claim to have proved an
   unreachable CALL; account explicitly for sound conservative fallbacks.
3. **Function emission and cloned body closure.** Run after the final clone,
   static-function and global-object pruning sequence. Inspect emitted
   externally visible functions, constructors, address-taken functions and
   truly referenced clones, but not discarded parse-only static bodies.
   One source body reused by multiple clones must be diagnosed per surviving
   Core incarnation, never by mutating shared source `ExpressionId` fields.
4. **Direct vs indirect attribute semantics.** Report only when callsite
   declaration provenance proves an attributed target. An indirect CALL
   remains unannotated unless an independent, sound devirtualization proof
   provides a precise target and retains the source attribute semantics.
   Do not infer an indirect target from a matching ABI signature or the mere
   address-taken set; GCC O0 and O2 intentionally differ here.
5. **Structured diagnostics.** `MinicDiagnostic` currently holds only a single
   fatal error, whereas GNU `warning` must be nonfatal and multiple messages
   may coexist. Provide a compiler diagnostic collection/callback with
   source span, level and exact owned message, keeping the public single-error
   API behavior compatible. Preserve `warning` output even when a coexisting
   `error` causes compilation to fail; do not silently downgrade or promote
   warnings. Diagnostic consumers belong before backend assembly emission.
6. **Oracle/gate expansion.** The 16-case matrix must become a real MiniC
   positive/negative gate **after** the above contracts exist. Add targeted
   `&&label`, reachable computed goto, asm goto, section-local clones,
   function-alias redeclarations and library embedding tests, plus all five
   ISA `-S` runs. No claim of complete GCC O0/O2 optimization equivalence is
   made; specify the selected MiniC post-Core-elimination semantics.

Until these contracts are implemented, deliberately leave MC-085 **OPEN**.
Do not implement Parser-wide CALL rejection, Core callee-symbol attribution,
or unconditional source-label-root diagnostic scanning. This audit establishes
real negative regression cases to keep a later fix safe.
