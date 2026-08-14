# Single-pass top-level declaration migration

## Why this exists

The materialized compiler is now driven directly by unchanged Linux 6.6.143 input. The committed-source Linux gate builds the current checked-in compiler, generates the real RISC-V `defconfig` Kbuild `init/main.i`, and invokes MiniC without any discovery source-rewrite stage.

The first current blocker is reproducible at `init/main.i:2961`:

```c
extern __attribute__((__format__(printf, 4, 5)))
void warn_slowpath_fmt(const char *file, const int line, unsigned taint,
                       const char *fmt, ...);
```

MiniC reports `expected type name` before the real declaration parser is reached.

The root cause is architectural rather than a missing `format` attribute. `format` is already classified as a supported diagnostic function attribute. The failure comes from `extern_declaration_is_function()`: it copies `MinicParser`, advances through a second semantic type parse to guess whether the declaration is a function, and assumes a type starts immediately after `extern`.

That pattern has two independent problems:

1. declaration grammar is parsed twice and can diverge by attribute placement, parenthesized declarators, GNU extensions, or later language growth;
2. the copied parser still shares the live `MinicC0Program`, so a semantic type probe can mutate real compiler state. For example, parsing a previously unseen `struct tag` may create a record entry before the actual declaration is parsed.

The fix must therefore remove semantic declaration probing, not merely teach the probe one more GNU spelling.

## Non-negotiable invariants

1. A top-level declaration's semantic type specifiers are parsed exactly once.
2. A classifier/lookahead path must never mutate `MinicC0Program`.
3. Function-vs-object dispatch is derived from an already parsed declarator shape, not from a second semantic parse.
4. Existing function/object initializer, redeclaration, linkage, section, visibility and RV64 behavior stays behind its current focused gates during this migration.
5. No new ordered source-rewrite stage is introduced. Checked-in C remains the compiler source of truth.
6. The unchanged Linux `init/main.i` frontier may move forward, but must not move backward.

## Important distinction: semantic probes vs lexical probes

Not every current `MinicParser probe = *parser` has the same priority.

### P0 semantic probes

These must be removed first:

- `static_declaration_is_function()`
- `extern_declaration_is_function()`

They call semantic type/declarator routines while sharing the live `Program` and therefore combine duplicate grammar with possible semantic side effects.

### Deferred lexical probes

Examples include:

- checking whether a record keyword is followed by a definition shape;
- scanning an array suffix to decide declaration vs definition;
- peeking at an attribute name such as `visibility` or `section`.

These still copy an unnecessarily large parser and should eventually use a small token cursor, but they do not currently need to be mixed into the declaration ownership repair. TokenCursor is therefore a later cleanup, not the current P0.

## Why DeclarationHead needs deferred attribute interpretation

Removing the old function/object probe exposes a dependency that the probe used to hide.

For:

```c
extern __attribute__((__format__(printf, 4, 5))) void f(const char *, ...);
```

when the parser reaches `__attribute__`, it has not parsed the declarator yet and therefore does not know whether the declaration target is a function or an object. Immediate function-specific attribute consumption is only possible today because the old probe has already guessed the target.

The replacement must therefore separate:

```text
attribute syntax parsing
        ↓
transient parsed/deferred attributes
        ↓
declarator classification
        ↓
target-specific validation/application
```

This does **not** require implementing the final persistent AST `AttributeSet` in the same change. A transient declaration-level representation is sufficient for the migration, provided it preserves descriptor identity and argument source spans rather than silently discarding them.

The existing `MinicParsedAttribute` and shared GNU attribute-list parser are the intended migration seam.

## Scope of the first implementation

Do not turn this change into a full C declarator rewrite.

The first implementation should introduce a top-level declaration-specifier/declarator seam only for syntax the materialized compiler already supports.

Conceptually:

```text
TopLevelDeclaration
  ├── DeclarationSpecifiers
  │     ├── storage/linkage: none | extern | static
  │     ├── inline
  │     ├── deferred prefix attributes
  │     └── base type (parsed once)
  │
  └── TopLevelDeclarator
        ├── pointer layers already supported by MiniC
        ├── name / parenthesized name
        ├── function suffix + parameters, when present
        └── enough shape information to select function vs object consumer
```

The type-specifier layer and the declarator layer should remain conceptually separate. In C, pointer operators belong to the declarator, not to the declaration specifiers. Avoid baking a `base+pointer+name` monolith into a permanent `DeclarationHead` type merely because the current parser historically uses `parse_type_name()` at top level.

## Migration sequence

### Step A — transient prefix-attribute capture

Add a small declaration-level representation that can receive `MinicParsedAttribute` entries from the existing shared GNU attribute parser without applying function/object target rules immediately.

For the first cut it must preserve enough information to later run the same target/class checks currently performed by function attribute consumers. Unknown, ABI/layout-changing, and unsupported semantic attributes must remain errors; do not convert this into a blanket attribute-ignore path.

### Step B — single-pass `extern` declaration dispatch

Use one real parser path for:

```text
extern
→ prefix attributes
→ type specifiers
→ declarator
→ classify parsed declarator
→ function consumer OR extern-object consumer
```

The Linux line-2961 declaration is the primary real-program acceptance case.

Preserve existing support for ordinary extern functions, extern objects, arrays, function-pointer objects, section attributes, visibility, multiple object declarators, asm labels and suffix function attributes. If preserving one of these requires a deliberate intermediate adapter, keep that adapter explicit rather than falling back to semantic re-parsing.

### Step C — validate before widening

Run:

- `check-fast`;
- frozen Lua/Linux focused semantics;
- cJSON / Parson / linenoise / SDS / Lua regression workflows;
- RV64 fixed stack-argument gate;
- unchanged Linux 6.6.143 `init/main.i` committed-source gate.

The expected result is that Linux moves beyond line 2961. The next Linux failure is evidence for the next decision, not an invitation to pre-implement a guessed feature list.

### Step D — migrate `static` dispatch

After the extern path is stable, reuse the same declaration-specifier/declarator seam for static function-vs-object dispatch and delete `static_declaration_is_function()`.

This deliberately avoids changing both large global-object consumers in one unvalidated commit while still converging on one architecture.

### Step E — global re-read

After both semantic probes are gone, re-read the parser globally before choosing the next structural target.

Current candidates remain:

- TargetInfo + DataLayout;
- shared ConstEval;
- persistent/deferred AttributeSet;
- StringId + hash-backed SymbolTable;
- unified array/object representation and InitPlan.

Do not assume their order survives the declaration migration unchanged.

## Things explicitly out of scope

- no Core IR introduction;
- no initializer rewrite;
- no TypeId conversion;
- no symbol-table replacement;
- no broad TokenCursor conversion;
- no target-layout rewrite;
- no new Linux feature implemented speculatively;
- no replacement of all declarator contexts in one patch.

## Exit criteria

This migration is complete when:

1. `extern_declaration_is_function()` and `static_declaration_is_function()` are gone;
2. top-level function/object classification consumes one parsed declaration/declarator result;
3. no semantic lookahead path can mutate `MinicC0Program` merely to classify a declaration;
4. all frozen gates remain green;
5. unchanged Linux `init/main.i` is compiled by committed source and its frontier is at or beyond the pre-migration frontier;
6. the next architectural step is chosen only after another global re-read.
