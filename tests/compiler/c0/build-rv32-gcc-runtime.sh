#!/usr/bin/env bash
# Pinned GNU libgcc + LLVM compiler-rt, not handwritten floating-point.
set -Eeuo pipefail
out="${1:?usage: build-rv32-gcc-runtime.sh WORKDIR}"
cc="${RISCV_GCC:-riscv64-linux-gnu-gcc}"
root=$(git rev-parse --show-toplevel)
vendor="$root/third_party/gcc-riscv-14.2.0"
llvm="$root/third_party/llvm-compiler-rt-19.1.7"
mkdir -p "$out"
"$cc" -march=rv32i -mabi=ilp32 -I"$vendor" -x assembler-with-cpp \
  -c "$vendor/muldi3.S" -o "$out/libgcc-mulsi3.o"
"$cc" -march=rv32i -mabi=ilp32 -I"$vendor" -x assembler-with-cpp \
  -c "$vendor/div.S" -o "$out/libgcc-divsi3.o"
objects=("$out/libgcc-mulsi3.o" "$out/libgcc-divsi3.o")
for name in muldi3 udivmoddi4 divdi3 udivdi3 moddi3 umoddi3 \
            clzsi2 clzdi2 fp_mode \
            adddf3 subdf3 muldf3 divdf3 comparedf2 \
            floatsidf floatunsidf fixdfsi fixunsdfsi \
            extendsfdf2 truncdfsf2; do
  src="$llvm/$name.c"
  test -f "$src"
  obj="$out/llvm-$name.o"
  "$cc" -std=gnu11 -O2 -ffreestanding -fno-builtin \
      -fno-stack-protector -fno-lto -fno-pic -nostdinc \
      -I"$vendor" -isystem "$llvm/freestanding-clang" \
      -isystem "$("$cc" -print-file-name=include)" \
      -march=rv32i -mabi=ilp32 -I"$llvm" -c "$src" -o "$obj"
  objects+=("$obj")
done
printf '%s\n' "${objects[@]}" >"$out/rv32-runtime-objects.rsp"
for obj in "${objects[@]}"; do
  riscv64-linux-gnu-readelf -h "$obj" | grep -Eq 'Class:[[:space:]]+ELF32'
done
echo "RV32_RUNTIME=PASS providers=GNU_libgcc_14.2.0,LLVM_compiler_rt_19.1.7 original_sources=${#objects[@]} march=rv32i mabi=ilp32"
