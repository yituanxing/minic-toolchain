# MiniC Toolchain

MiniC is a software-driven compiler toolchain rebuilt in ISO C11 for practical use, staged self-hosting, extensibility, and compiler education.

Real workloads decide what is implemented next. Language standards, target ABIs, differential testing, explicit ownership rules, and documented architectural boundaries determine whether an implementation is correct.

Chinese introduction: [`README.zh-CN.md`](README.zh-CN.md)

## Active compiler boundary

The current track replaces only the C compilation stage:

```text
C source
  -> external RISC-V GCC preprocessing
  -> MiniC compiler: preprocessed C (.i) to RV64 assembly (.s)
  -> external GNU assembly and linking
  -> QEMU RISC-V user-mode execution
```

External GCC is an explicit auxiliary tool. It may preprocess, assemble, link, and provide CRT/libc, but it must not compile a C function on MiniC's behalf.

Native preprocessing, assembly, linking, libc replacement, and full self-hosting are separate later milestones.

## Current implementation

The C implementation now includes:

- token and source-span models plus a dedicated lexer;
- a modular parser split by expressions, statements, functions, types, records, typedefs, globals, members, and constants;
- typed expressions with lvalue/rvalue distinctions, explicit signed/unsigned integer identity, and equal-rank integer conversions;
- lexical block scopes and stable Program-owned local objects;
- integer arithmetic, comparisons, bitwise XOR, conditions, `if`, `while`, normalized bounded `for` loops, assignments, general expression statements with discarded values, prefix increment, pointers, fixed arrays, array and pointer subscripting, pointer arithmetic, comma-separated local declarations, and const-qualified local initialization;
- function prototypes, legal forward calls, direct and nested calls, recursion, mutual recursion, and zero through eight integer register arguments;
- `void`, `const`, named records, typedef-backed record fields, local record objects, pointer-to-record member lookup, scalar member lvalues, array-member decay, recursive array typedefs, static read-only global arrays, global array expression lookup with local-name shadowing, and internal functions;
- RV64 scalar/array/record object layout, field offsets and aggregate alignment, call-safe stack frames, signed/unsigned loads and arithmetic, `xor` lowering, member base-plus-offset addressing, expression-statement evaluation with result discard, shared scaled address calculation for local arrays, pointer values, and global symbols, assembly emission, and internal symbol visibility;
- debug, release `-Werror`, ASan/UBSan, RV64/QEMU, and GCC/MiniC differential gates.

Twenty-nine executable C programs are permanently compared between a full GCC reference lane and the MiniC lane by exit status, standard output, and standard error. The matrix includes isolated loop-counter/body tests, high-bit unsigned comparison/division/remainder coverage, pointer-parameter/local-pointer subscript reads and writes, const-local initialization/readback, static global-array lookup with local-name shadowing, signed/unsigned bitwise-XOR semantics, pointer record members with non-zero field offsets, and expression statements covering void calls, discarded return values, pointer side effects, and side-effect-free arithmetic.

## First external project

The first pinned upstream driver is `kokke/tiny-AES-c`, AES-128 ECB configuration. Upstream source files are downloaded at fixed Git blob identities and are not patched for MiniC.

The compiler has progressed through the upstream declarations, typedef arrays, static lookup tables, internal functions, `void` definitions, unsigned declaration lists, the first `KeyExpansion` loop, pointer-parameter subscripting, const-qualified local initialization, expression references to the static global `sbox` table, bitwise XOR, typedef-backed record fields, record layout, the first `ctx->RoundKey` pointer member access, and the standalone `KeyExpansion(ctx->RoundKey, key);` expression statement. The current verified frontier is continuing postfix subscripting from the parenthesized dereference in `(*state)[i][j]` inside `AddRoundKey`.

The target shim is temporary evidence scaffolding, not an implementation of `uint8_t`. True byte-sized type identity, object layout, loads/stores, scaling, and conversions remain separate capabilities that must be implemented before AES execution can be considered correct.

The external project is not complete yet. Completion requires the pinned AES-128 ECB core and a test-vector harness to pass the GCC/MiniC differential oracle without MiniC-specific source changes.

## Build and validation

```sh
make
make check-fast
make sanitize
```

With a RISC-V Linux compiler and QEMU user-mode executor:

```sh
make check-runtime \
  RISCV_CC=riscv64-linux-gnu-gcc \
  QEMU_RISCV64=qemu-riscv64 \
  REQUIRE_RISCV_RUNTIME=1
```

GitHub Actions runs the complete clean-checkout gate on Ubuntu 24.04, including the pinned tiny-AES frontier.

## Project rules

- Real software drives priority; standards and ABI documents define semantics.
- The active design is a modular monolith, not a universal compiler state object.
- Public interfaces stay small; subsystem internals may be split into focused files.
- Abstractions require evidence from ownership, lifetime, platform variation, or multiple implementations.
- Temporary architectural debt is allowed only when it is visible, bounded, and has concrete exit criteria.
- Production, architecture, build, and migration commits use bilingual English/Chinese bodies and record actual validation.

See:

- [`docs/architecture/principles.md`](docs/architecture/principles.md)
- [`docs/architecture/compiler-development-roadmap.md`](docs/architecture/compiler-development-roadmap.md)
- [`docs/standards/implementation-language.md`](docs/standards/implementation-language.md)
- [`docs/standards/validation-toolchain.md`](docs/standards/validation-toolchain.md)
- [`docs/DEVIATIONS.md`](docs/DEVIATIONS.md)
- [`docs/milestones/compiler-c3-tiny-aes-frontier.md`](docs/milestones/compiler-c3-tiny-aes-frontier.md)
