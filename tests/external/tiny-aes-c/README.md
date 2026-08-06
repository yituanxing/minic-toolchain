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

MiniC now passes the upstream declarations, `struct AES_ctx`, typed and `const` prototypes, multidimensional typedef arrays, static read-only lookup tables, static internal functions, `void` definitions, explicit unsigned integer identity, comma-separated unsigned declarations, the first `KeyExpansion` loop, pointer-parameter subscripting, const-qualified local initialization, expression lookup of the static global `sbox` table, bitwise XOR, typedef-backed record fields, RV64 record layout, pointer member access in `AES_init_ctx`, the standalone `KeyExpansion(ctx->RoundKey, key);` expression statement, the repeatable postfix chain `(*state)[i][j]`, compound XOR assignment in `AddRoundKey`, the complete integer bit expression in `xtime`, and the empty-condition `for` loop with `break` in `Cipher`.

The unbounded-loop control-flow slice adds a dedicated `break` token and statement identity, explicit parser loop-depth tracking, empty `for` conditions normalized to unconditional loops, and RV64 lowering to the innermost loop-end label. Ordinary `while` statements still require an integer condition. Loop context is preserved through nested `if` blocks, while nested loops replace the active break target with their own end label.

Focused coverage verifies that two nested unbounded loops receive distinct exit labels, no condition guard is emitted for an omitted `for` condition, a `break` skips the normalized loop-tail update, and `break` is rejected outside a loop or without a terminating semicolon. A separate negative fixture prevents empty conditions from leaking into ordinary `while` statements.

The exact pinned failure has advanced to the prefix decrement update in `InvCipher`:

```c
for (round = (10 - 1); ; --round)
```

MiniC currently accepts only prefix increment as the normalized `for` update. It therefore reports `for update requires prefix increment` at preprocessed line 228, column 28. Prefix decrement tokenization, validation, normalization, and lowering belong to the next independent capability slice and are intentionally not included in the unbounded-loop branch.

The permanent GCC/MiniC execution matrix now contains thirty-three programs. The `unbounded_for_break` program covers nested empty-condition loops, innermost-loop targeting, breaks through nested branches, and skipped loop-tail updates; both lanes exit with status 31 and produce empty output streams. The earlier integer bit-operation program continues to exit 58 in both lanes.

## Remaining acceptance debt

Before tiny-AES can be accepted as a real execution result, the temporary integer-name shim must be replaced by actual narrow integer semantics, including:

- one-byte type size and alignment;
- byte-sized local, global, array, and record-field layout;
- RV64 byte loads and stores;
- byte-based pointer/subscript scaling;
- integer promotion, conversion, and truncation rules;
- matching GCC/MiniC AES-128 ECB test-vector execution.

This probe is intentionally a moving frontier, not an execution acceptance test. Once the upstream core compiles, it must be replaced by assembly/linking and a real AES-128 ECB differential execution harness.
