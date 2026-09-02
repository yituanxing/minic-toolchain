#!/bin/sh
set -eu

: "${BUILD_DIR:?BUILD_DIR must be set}"
AS=${RISCV_AS:-riscv64-linux-gnu-as}
HOST_CC=${HOST_CC:-cc}

work="$BUILD_DIR/tests/elf/reader-a0"
mkdir -p "$work"

cat >"$work/input.s" <<'EOF'
.text
.globl caller
.type caller, @function
caller:
  call target
  ret
.size caller, .-caller

.globl target
.type target, @function
target:
  ret
.size target, .-target

.data
.globl value
.type value, @object
value:
  .word 42
.size value, 4
EOF

"$HOST_CC" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
  -Wstrict-prototypes -Wmissing-prototypes -Werror \
  -Ielf/include elf/src/reader.c tests/elf/reader_test.c \
  -o "$work/reader-test"

"$AS" -march=rv64imac -mabi=lp64 -o "$work/input64.o" "$work/input.s"
"$AS" -march=rv32imac -mabi=ilp32 -o "$work/input32.o" "$work/input.s"

"$work/reader-test" "$work/input64.o" 64
"$work/reader-test" "$work/input32.o" 32

echo "MINIELF_READER_A0=PASS formats=ELF64-RISCV,ELF32-RISCV source=GNU-as"
