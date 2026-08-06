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
- a modular parser split by expressions, postfix operations, statements, functions, types, records, typedefs, globals, members, and constants;
- explicit Parsed and Normalized AST contracts: storage ownership, type identities, value categories, child-before-parent topology, calls, statements, blocks, functions, records, arrays, aliases, and globals are verified at pipeline boundaries; Cast normalization lives in one translation unit, rebuilds expression IDs topologically and transactionally, and uses a normalized-only `BITCAST` for pointer bit-pattern preservation;
- typed expressions with lvalue/rvalue distinctions, decimal and hexadecimal integer constants through one canonical parser, explicit signed/unsigned identity, CHAR/INT integer ranks, native `unsigned char`, C integer promotions from `unsigned char` to `int`, and safe first-level `T *` to `const T *` qualification conversion without const removal or unsafe nested conversion;
- lexical block scopes and stable Program-owned local objects;
- integer arithmetic, comparisons, left/right shifts, bitwise AND/XOR, conditions, `if`, `while`, normalized conditioned and empty-condition `for` loops, innermost-loop `break`, ordinary and compound-XOR assignments, general expression statements with discarded values, bounded prefix increment/decrement updates, pointers, fixed and recursive arrays, repeatable postfix subscripting on suitable expression results, pointer arithmetic restricted to complete object pointees, comma-separated local declarations, and const-qualified local initialization;
- function prototypes, legal forward calls, direct and nested calls, recursion, mutual recursion, and zero through eight integer register arguments;
- `void`, `const`, named records, typedef-backed record fields, local record objects, pointer-to-record member lookup, scalar member lvalues, array-member decay, recursive array typedefs, static read-only global arrays, global array expression lookup with local-name shadowing, and internal functions;
- RV64 byte/int/pointer scalar layout, shared type-size/alignment queries, one-byte array and record-field layout, `lbu`/`sb` byte accesses, 8-bit truncation, `.byte` global-table emission, one-byte pointer/subscript scaling, call-safe stack frames, signed/unsigned loads and arithmetic, `sllw`/`sraw`/`srlw` shift lowering, `and`/`xor` lowering, normalized `BITCAST` pass-through lowering, single-evaluation read-modify-write lowering for `^=`, conditioned and unconditional loop lowering, normalized add/subtract loop updates, innermost loop-exit targets for `break`, member base-plus-offset addressing, expression-statement evaluation with result discard, power-of-two and arbitrary aggregate pointer/subscript scaling through shared layout queries, assembly emission, and internal symbol visibility;
- debug, release `-Werror`, ASan/UBSan, RV64/QEMU, and GCC/MiniC differential gates.

Thirty-eight executable C programs are permanently compared between a full GCC reference lane and the MiniC lane by exit status, standard output, and standard error. The matrix includes isolated loop-counter/body tests, nested empty-condition loops with innermost `break` and skipped tail updates, a descending loop whose `break` skips the decrement tail, high-bit unsigned comparison/division/remainder coverage, native unsigned-char truncation/promotion/layout/access tests, hexadecimal expression constants, pointer-parameter/local-pointer subscript reads and writes, const-local initialization/readback, static global-array lookup with local-name shadowing, signed/unsigned bitwise-XOR semantics, signed and unsigned shift behavior, bitwise AND, pointer record members with non-zero field offsets, general expression statements, multidimensional pointer-to-array subscripting with a non-power-of-two 12-byte row stride, 12-byte record and pointer-to-array arithmetic in both operand orders and subtraction, and compound-XOR assignment with a side-effecting complex target that must be evaluated once.

## First external project — completed

The first pinned upstream driver is `kokke/tiny-AES-c`, AES-128 ECB configuration. Upstream source files are downloaded at fixed Git blob identities and are not patched for MiniC.

Despite the repository name, the cryptographic implementation is intentionally concentrated in one principal C source file, `aes.c`; `aes.h` is its public interface and the other C material is test/example code. MiniC compiles the complete pinned `aes.c` core with a real `typedef unsigned char uint8_t`, emits RV64 assembly, and links it into a static RISC-V executable.

The permanent independent harness verifies all 176 AES-128 expanded round-key bytes, first-round intermediate states, the standard AES-128 ECB ciphertext, and decryption back to the original plaintext. GitHub Actions clean-checkout run #556 executed both a full GCC reference executable and the MiniC executable under QEMU; both exited 0 with empty stdout and stderr. The MiniC-generated object was 26,016 bytes. All 37 general differential programs also passed.

This satisfies the declared completion criteria for the first external project. The AES-128 ECB configuration is now frozen as a permanent regression gate. CBC/CTR expansion is not required for this milestone and would be a separate future workload choice.

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

GitHub Actions runs the complete clean-checkout gate on Ubuntu 24.04, including the pinned tiny-AES AES-128 ECB differential execution gate.

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
