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

MiniC now passes the upstream declarations, `struct AES_ctx`, typed and `const` prototypes, multidimensional typedef arrays, static read-only lookup tables, static internal functions, `void` definitions, explicit unsigned integer identity, comma-separated unsigned declarations, the first `KeyExpansion` loop, pointer-parameter subscripting, const-qualified local initialization, expression lookup of the static global `sbox` table, bitwise XOR, typedef-backed record fields, RV64 record layout, pointer member access in `AES_init_ctx`, the standalone `KeyExpansion(ctx->RoundKey, key);` expression statement, the repeatable postfix chain `(*state)[i][j]`, and compound XOR assignment in `AddRoundKey`.

Compound XOR assignment is represented as a dedicated read-modify-write statement. RV64 code generation evaluates the target lvalue address exactly once, saves it, loads the old value, applies the common integer conversion, evaluates the right operand, performs XOR, converts back to the target type, and stores through the saved address. A focused side-effect fixture requires exactly one function call while computing a complex subscript target.

The exact pinned failure has advanced to the left-shift operator in `xtime`:

```c
return ((x<<1) ^ (((x>>7) & 1) * 0x1b));
```

The lexer currently emits two `<` tokens. Binary parsing consumes the first as a comparison and reaches the second while expecting its right operand, reporting `expected expression` at preprocessed line 137, column 14. The next integration must add shift tokens and precedence, integer-only type rules, and RV64 word-shift lowering. The same expression also exposes right shift and bitwise AND immediately afterward, so those operations should be implemented and validated as one bounded integer-bit-operations slice.

Compound XOR support is protected by longest-match token tests, integer and const diagnostics, an assembly gate, a single-evaluation side-effect gate, and thirty-one GCC/MiniC differential programs. The `compound_xor_assignment` program covers signed and unsigned targets and verifies that a function call used to compute the target subscript executes once; GCC and MiniC both exit with status 205 and produce empty output streams.

## Remaining acceptance debt

Before tiny-AES can be accepted as a real execution result, the temporary integer-name shim must be replaced by actual narrow integer semantics, including:

- one-byte type size and alignment;
- byte-sized local, global, array, and record-field layout;
- RV64 byte loads and stores;
- byte-based pointer/subscript scaling;
- integer promotion, conversion, and truncation rules;
- matching GCC/MiniC AES-128 ECB test-vector execution.

This probe is intentionally a moving frontier, not an execution acceptance test. Once the upstream core compiles, it must be replaced by assembly/linking and a real AES-128 ECB differential execution harness.
