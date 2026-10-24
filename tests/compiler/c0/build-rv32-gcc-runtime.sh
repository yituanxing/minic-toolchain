#!/usr/bin/env bash
# Reuse GNU's pinned, upstream-unmodified libgcc 14.2.0 RV32I arithmetic.
# The same sources already provide MiniC's accepted RV64I libcalls.
# No homegrown MiniC division/multiplication implementation.
set -Eeuo pipefail
out="${1:?usage: build-rv32-gcc-runtime.sh WORKDIR}"
cc="${RISCV_GCC:-riscv64-linux-gnu-gcc}"
root=$(git rev-parse --show-toplevel)
vendor="$root/third_party/gcc-riscv-14.2.0"
test -f "$vendor/div.S"
test -f "$vendor/muldi3.S"
mkdir -p "$out"
"$cc" -march=rv32i -mabi=ilp32 -I"$vendor" -x assembler-with-cpp \
  -c "$vendor/muldi3.S" -o "$out/libgcc-mulsi3.o"
"$cc" -march=rv32i -mabi=ilp32 -I"$vendor" -x assembler-with-cpp \
  -c "$vendor/div.S" -o "$out/libgcc-divsi3.o"
echo "RV32_RUNTIME=PASS provider=GNU_libgcc_14.2.0 original_sources=2 march=rv32i mabi=ilp32"
