# tiny-AES-c external frontier

This directory tracks the first independently maintained upstream project used to drive MiniC.

## Upstream identity

- Repository: `kokke/tiny-AES-c`
- Commit: `23856752fbd139da0b8ca6e471a13d5bcc99a08d`
- License: Unlicense (`unlicense.txt`)
- Initial configuration: AES-128 ECB only (`ECB=1`, `CBC=0`, `CTR=0`)

Pinned Git blob identities:

| File | Git blob SHA-1 |
|---|---|
| `aes.c` | `4481f7b24ec964019d38669842913fd571d28ba3` |
| `aes.h` | `b29b6683549632676ec11c06eb86efd02964db57` |
| `unlicense.txt` | `68a49daad8ff7e35068f2b7a97d643aab440eaec` |

## Rules

1. The downloaded upstream files are not edited for MiniC.
2. The RISC-V GCC preprocessor remains outside the compiler-under-test boundary.
3. Small target-environment shim headers may define standard integer names while native system headers remain outside the active subset.
4. A shim is temporary evidence scaffolding and must not be presented as native type-width support.
5. The probe verifies every upstream Git blob before preprocessing.
6. The expected frontier advances only after the previous capability has a production implementation and permanent focused coverage.
7. Completion requires the pinned AES-128 ECB core and a test-vector harness to pass the GCC/MiniC differential oracle without MiniC-specific source patches.

## Current frontier

MiniC now passes the upstream declarations, `struct AES_ctx`, typed and `const` prototypes, multidimensional typedef arrays, static read-only lookup tables, static internal functions, `void` definitions, explicit unsigned integer identity, comma-separated unsigned declarations, the first `KeyExpansion` loop, pointer-parameter subscripting, const-qualified local initialization, expression lookup of the static global `sbox` table, bitwise XOR, typedef-backed record fields, RV64 record layout, pointer member access in `AES_init_ctx`, and the standalone `KeyExpansion(ctx->RoundKey, key);` expression statement.

The exact pinned failure is the first postfix subscript after a parenthesized dereference in `AddRoundKey`:

```c
(*state)[i][j] ^= RoundKey[(round * Nb * 4) + (i * Nb) + j];
```

MiniC parses `(*state)` as a valid parenthesized dereference expression, but postfix parsing is still embedded in selected local/global/member paths. It therefore stops before the first `[i]`. The next integration must centralize repeatable postfix subscripting so any suitable expression result can become a subscript base; only after that boundary will the compound `^=` operator become the active frontier.

Expression-statement support is protected by focused call/assignment/semicolon gates and twenty-nine GCC/MiniC differential programs. The new `expression_statement` program covers a void call, a value-returning call whose result is discarded, pointer side effects, and a side-effect-free arithmetic expression; both lanes exit with status 10 and produce empty output streams. The parser also copies target type identity before parsing an assignment right-hand side, preventing pointers into growable Program-owned expression storage from surviving an AST reallocation boundary.

## Remaining acceptance debt

Before tiny-AES can be accepted as a real execution result, the temporary integer-name shim must be replaced by actual narrow integer semantics, including:

- one-byte type size and alignment;
- byte-sized local, global, array, and record-field layout;
- RV64 byte loads and stores;
- byte-based pointer/subscript scaling;
- integer promotion, conversion, and truncation rules;
- matching GCC/MiniC AES-128 ECB test-vector execution.

This probe is intentionally a moving frontier, not an execution acceptance test. Once the upstream core compiles, it must be replaced by assembly/linking and a real AES-128 ECB differential execution harness.
