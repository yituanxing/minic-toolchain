# MiniLD RV32I ILP32 C runtime ladder (2026-11-04)

The ordinary five-stage `probe-minild-gcc-runtime-ladder.sh` uses Linux
glibc's GCC CRT and static libraries on ELF64 systems. The installed
`riscv64-linux-gnu-gcc` target is **not** a general RV32I/ILP32 glibc
toolchain. Never label an `rv64gc/lp64d` program as an RV32 runtime pass.

For real **Linux ELF32** on RV32I, the project already has a working
upstream Picolibc integration for cJSON/zlib/Lua. This test reuses its exact
Linux entry and syscall boundary instead of inventing another startup ABI.
It is still Picolibc (a real C library), **not Linux glibc**, and does not
claim a complete POSIX/Linux distribution runtime.

## Requirements

The Ubuntu 24.04 CI package set is already exercised in
`.github/workflows/minic-rv32-cjson-runtime.yml`:

```sh
sudo apt-get install -y --no-install-recommends \
  gcc-riscv64-unknown-elf picolibc-riscv64-unknown-elf \
  gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu qemu-user
```

The source code in `tests/compiler/c0/rv32-linux-picolibc-{start.S,
syscalls.c,posix.c,errno.c}` supplies the GNU GCC-built Linux-specific
program entry, PT_TLS initialization, and Linux syscalls. Real
`rv32i/ilp32/libc.a`, `libm.a` and GCC `libgcc.a` are resolved from the
installed external toolchain, never substituted by an RV64 archive. The
Picolibc bare-metal default linker script is deliberately not used.

## Execution and acceptance

```sh
make -j4 MODE=release CFLAGS=-Werror \
  build/release/bin/minic-as build/release/bin/minic-ld
MINIAS="$PWD/build/release/bin/minic-as" \
MINILD="$PWD/build/release/bin/minic-ld" \
bash tests/linker/probe-rv32-picolibc-ladder.sh
```

Before the five ordinary stages, `tls` is a blocking extra **linker** proof.
Its `tests/linker/fixtures/rv32-picolibc-tls-le.c` contains initialized
`.tdata`, zero-initialized `.tbss`, and a real Picolibc `errno` TLS access.
GNU GCC/GAS produce one genuine ELF32 TLS object preserving both sections
and all four local-exec
relocations (`R_RISCV_TPREL_HI20`, `R_RISCV_TPREL_ADD`,
`R_RISCV_TPREL_LO12_I`, `R_RISCV_TPREL_LO12_S`); both linkers must
produce a valid PT_TLS image; and the reused Linux entry must initialize
the actual `tp` and execute the result under QEMU. This is a local-exec
psABI correctness test, not a substitute implementation of TLS. The same
GNU-produced TLS object is reused by both linkers. MiniAS currently cannot
parse GNU GCC's `%tprel_*` assembler operands (`invalid-riscv-mcinst`),
so **MiniAS TLS-source acceptance remains independently UNSUPPORTED**.
The five ordinary program objects below still all originate from MiniAS;
linker TLS testing must not be misreported as MiniAS TLS syntax support.

For each ordinary stage (`exit`, `control`, `memory`, `multitu`, `io`):

1. External GNU GCC with `-march=rv32i -mabi=ilp32` emits ordinary C
   assembly using the authentic Picolibc headers.
2. MiniAS assembles that assembly into a genuine ELF32/ILP32 object.
3. GNU BFD and MiniLD independently receive the **same MiniAS objects**,
   Linux startup/syscall objects, authentic libc/libm/libgcc archives,
   link order, `-static`, `-e _start` and `-m elf32lriscv`.
4. QEMU `qemu-riscv32` executes both resulting ELF32 ET_EXEC programs.
   Exit code, expected stdout, stderr and byte-exact GNU/MiniLD parity
   all must pass for the stage to be accepted.

Each stage preserves GCC/MiniAS/linker diagnostics, assembly, object hashes,
both linked images and runtime evidence under
`build/minild-rv32-picolibc-ladder/`. A genuine GNU-side limitation, such
as a missing or unsupported `tmpfile()` on the external Picolibc target,
is recorded as a failed or blocked stage, not silently considered success.

## Verified status

The current development host has neither `riscv64-unknown-elf-gcc` nor
`qemu-riscv32`, so the initial local invocation correctly terminates with:

```
MINILD_RV32_PICOLIBC=ERROR reason=missing_tool:riscv64-unknown-elf-gcc
```

That is **not** a runtime PASS. The first five-stage CI execution was
[run #41826451310](https://github.com/yituanxing/minic-toolchain/actions/runs/41826451310),
with artifact `13319676179`: the authentic RV32 Linux/Picolibc programs
linked and ran with GNU BFD, while MiniLD linked **0/5**. Its earliest
rejected relocation was `R_RISCV_TPREL_HI20` (numeric type 29), from
the GNU-GCC-generated Picolibc `errno` TLS local-exec code. GNU's
ELF32 program contains PT_TLS and a mapped ELF/program-header table;
both details are now mandatory MiniLD assertions. The new explicit TLS
fixture has **not** yet run in CI. Correct status is therefore a linker
capability gap under active repair, not a five-architecture success.

Even a subsequent five-stage Picolibc PASS is a prerequisite, not a
replacement, for RV32 musl or complete Linux kernel linking/boot acceptance.
