# tiny-AES-c external frontier

This directory tracks the first upstream project used to drive MiniC from real, independently maintained C source.

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
3. Small target-environment shim headers may model standard integer names while MiniC lacks native standard headers.
4. The frontier probe must verify every upstream Git blob before preprocessing.
5. The current expected diagnostic is advanced only when the previous language boundary has a production implementation and permanent regression coverage.
6. Completion means the pinned upstream AES-128 ECB core and a NIST-derived harness pass the GCC/MiniC differential Oracle without MiniC-specific source patches.
