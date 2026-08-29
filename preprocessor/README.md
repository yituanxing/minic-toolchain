# MiniCPP preprocessor component

Public tool identity: `minic-cpp`.

Ownership: C preprocessing from source/header tokens to the preprocessed translation
unit consumed by `minic-cc`. MiniCPP is an independent tool with independent
sources. It must not share implementation ownership with MiniC or MiniAS.

## Correctness contract

The frozen historical Python/V218 design is the reference model: for a fixed
reference GCC version, fixed command line, fixed source tree and fixed headers,
the primary acceptance gate is **byte-identical preprocessed output**.

```text
same .c + same headers + same -D/-U/-I flags

reference GCC -E  -> reference.i
MiniCPP           -> mini.i

cmp reference.i mini.i
```

Whitespace, newlines, comment removal, token spelling and line markers therefore
belong to the exact-output contract. A normalized preprocessing-token comparison
is retained only as a diagnostic layer to distinguish serializer differences
from real macro/preprocessing semantic errors.

## Architecture

The implementation is deliberately separate:

```text
preprocessor/include/   public MiniPP API
preprocessor/src/       file/token/macro/directive/include ownership
tools/minic-cpp/        CLI only
tests/preprocessor/     differential and corpus gates
```

The historical C-rewrite boundary is retained: file buffering and preprocessing
tokens form the base; include resolution/raw-header caching and full macro
semantics are layered above them. Macro expansion must preserve the Python-era
prescan/rescan, active-macro hide-set/cycle policy, stringize and token-paste
semantics rather than devolving into textual substitution.

## M0

M0 intentionally starts with a narrow exact-output slice:

- `-E -P -undef -nostdinc`
- ordinary source text
- comment removal
- object-like `#define` / `#undef`
- recursive object-macro rescan with active-macro suppression
- `#if` for simple integer/object-macro expressions
- `#ifdef` / `#ifndef` / `#elif` / `#else` / `#endif`
- command-line `-D` / `-U`

`make check-minipp-a0` compares every M0 case byte-for-byte with GCC.

Function-like macros, include search/cache, full preprocessing expressions,
predefined macros and GCC-compatible line markers are the next real-program
frontier. Linux expansion should then reuse the historical staged gates
(95/18/72/full), followed by the same sample/failure-pool/full-corpus method used
for MiniC and MiniAS.

Performance work must avoid repeated full rescans: historical Python profiling
showed macro expansion/rescan dominating preprocessing time, so the C design
should keep token-indexed lookup and cheap "no expandable identifier" exits in
mind from the beginning.
