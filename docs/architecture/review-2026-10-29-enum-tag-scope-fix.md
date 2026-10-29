# C tag namespace and lexical-scope fix — 2026-10-29

## Ownership and verification

Changes confined to the five owned parser tag files:
`src/frontend/parser_enum.c`, `parser_record.c`, `parser_type.c`,
`parser_internal.h`, `parser_core.c`, plus dedicated `tests/compiler/c0`
fixtures and probe scripts. Other agents own Core, AArch64, parser labels,
and unrelated type-system changes. **Not committed or pushed** here.

## P1: enum tag scope was global by accident — fixed

Previous implementation of `minic_parser_find_enum_tag` searched an
append-only `enum_tags` array, and both definitions and references called the
same global `get_or_create_enum_tag`. `minic_parser_end_scope` restored the
record-tag and enumerator stacks but not the enum-tag stack.

This produced two independent GNU C oracle discrepancies:

1. `enum E { A }; void f(void) { enum E { B }; }` — GCC accepts; old MiniC
   rejected `duplicate enum definition`, because the inner definition
   accidentally completed the **file-scope** enum a second time.
2. `void f(void) { { enum E { A }; } sizeof(enum E); }` — GCC rejects use of an
   incomplete `enum E`; old MiniC accepted because the out-of-scope completed
   tag leaked from the inner block.

### Semantic correction

- `MinicParserScopeFrame::tag_begin` snapshots one shared namespace stack;
  `minic_parser_end_scope` restores it. Distinct `MinicEnumId` and
  `MinicRecordId` objects remain stable in the C0 program, so already-parsed
  types retain identity.
- `minic_parser_resolve_tag` performs one semantic kind+scope lookup through
  shared bindings; callers no longer perform separate record/enum scans.
- Definitions `enum E { ... }` and standalone declarations `enum E;` reuse
  only the current-scope entity, creating a shadow binding if only an outer
  one exists. Ordinary type references `enum E variable;` continue to use
  nearest visible binding.
- Unnamed enum definitions, same-scope forward completion and duplicate
  definition checks are unchanged.

### Tests, exact evidence

`make -j8 build/debug/bin/minic-cc build/debug/bin/minic` — exit 0.

`sh tests/compiler/c0/run-enum-tag-scopes.sh` — **PASS**, confirming:

- GCC `-std=gnu11 -Wall -Wextra -Werror -fsyntax-only` accepts positive
  fixture and rejects the negative escaped tag fixture.
- MiniC `-S --target=x86_64-linux` emits valid x86 assembly; host GCC links
  it and native execution returns zero. Nested scope shadowing, re-exposure
  of the outer enum, separate function-scope shadow, and forward-definition
  identity are covered.
- MiniC rejects the escaped tag fixture with an `incomplete` diagnostic.

Existing independent regressions: **PASS** for
`run-enum-tag-type-references.sh`, `run-enum-forward-completion.sh`,
`run-block-scope-record-tags.sh`, `run-typedef-enum-definitions.sh`,
`run-gnu-enum-entity.sh`, `run-enum-mode-byte.sh`, and
`run-block-scope-typedef.sh`.

Two older tests (`run-enum-constant-scopes.sh` and
`run-enum-constant-expressions.sh`) exit 1 **after successfully invoking
MiniC**, on brittle RV64 assembly-output `grep` expectations. The former
requires a `first:` symbol but current Core optimization eliminates that
function; the latter requires literal `li a0, 256` although the current
output has folded the entire function to `li t0, 0`. No parser assertion or
enum compilation diagnostic failed. These pre-existing scripts need
semantics-based assertions in a separate Core/test maintenance task; don't
misclassify them as proof of parser regression.

## P1: struct, union and enum must share one namespace — fixed

**Before fix**, two GCC 12 versus MiniC differential probes confirmed:

1. File scope: `enum ExampleTag { A }; struct ExampleTag { int member; };`.
   GCC exits 1, `defined as wrong kind of tag`; MiniC exits **0** and emits
   x86 assembly despite the C constraint violation.
2. Outer `enum ExampleTag` followed by inner-block `struct ExampleTag` and
   then inner `enum ExampleTag *p;`. GCC exits 1, wrong tag kind; MiniC
   exits **0**, incorrectly binding `p` to the hidden outer enum.

Root cause: separate `MinicParserRecordTag` / `MinicParserEnumTag` arrays
incorrectly modeled two namespaces, while ISO C gives `struct` / `union` /
`enum` one shared tag namespace. The fix removes both arrays in favor of
`MinicParserTagBinding { name_span, kind, entity }` with `tag_begin` scope
bookkeeping in `parser_core.c`. The shared `minic_parser_resolve_tag` checks
kind and nearest/current lexical visibility, and `minic_parser_bind_tag`
enforces current-scope uniqueness across all three kinds. The two specific
tag parsers own only entity creation/completion and type construction.

For references, `enum E x;` and `struct E *p;` consult the nearest visible
tag, reject a visible wrong kind, and do not silently fall through to an
outer same-kind tag. A definition can shadow a different-kind outer tag,
but cannot redeclare a different kind in the **same scope**. Standalone
record forward declarations `struct E;` also introduce current-scope
bindings and can shadow other kinds. GNU's nonstandard incomplete
`enum E;` is subtler: GCC rejects it when an outer struct/union E remains
visible, while accepting same-kind forward shadow; the implementation
checks that separately. Record forward completion and struct/union
distinctions remain checked.

### Positive and negative GCC differential matrix

`sh tests/compiler/c0/run-tag-namespace-scopes.sh` — **PASS**:

| Fixture | GCC oracle | MiniC | Purpose |
| --- | --- | --- | --- |
| `tag_namespace_scopes.c` | accepts | accepts and runs (x86) | enum→struct→enum nested shadow, struct→enum, union→struct, struct forward shadow, global restoration |
| `invalid_tag_kind_enum_struct.c` | rejects | rejects | same-scope enum→struct collision |
| `invalid_tag_kind_struct_enum.c` | rejects | rejects | same-scope struct→enum collision |
| `invalid_tag_kind_struct_union.c` | rejects | rejects | same-scope struct→union collision |
| `invalid_tag_kind_union_struct.c` | rejects | rejects | same-scope union→struct collision |
| `invalid_tag_kind_shadow_enum.c` | rejects | rejects | inner struct hides outer enum |
| `invalid_tag_kind_shadow_struct.c` | rejects | rejects | inner enum hides outer struct |
| `invalid_tag_kind_forward_enum_record.c` | rejects | rejects | GNU incomplete enum forward cannot hide outer record |

The valid positive fixture also compiles **PASS** with all five registered
targets: riscv32-linux, riscv64-linux, x86_64-linux, aarch64-linux, and
loongarch64-linux (`-S` only outside host-native x86).

Remaining design consideration: lookup is currently a shared reverse linear
scan, as with the old tag arrays. Hash indexing can be investigated with
genuine MiniC Linux compile profiles; no unmeasured performance claim is made
here. Enumerator names and ordinary local bindings remain in their distinct
C ordinary-identifier namespace.
