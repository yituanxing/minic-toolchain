#!/usr/bin/env bash
# Original pinned tiny-AES-C AES128/ECB real-software frontier for RV32I.
# Preserve original fixture and source and report the first blocked compiler
# family. A blocked frontier is NOT success: only QEMU/vector matching is PASS.
set -Eeuo pipefail
: "${MINIC_CC:?MINIC_CC must name the compiler binary}"
: "${MINIAS:=${MINIC_CC%/minic-cc}/minic-as}"
root=$(git rev-parse --show-toplevel)
aes="$root/tests/vendor/tiny-aes-c/upstream"
fixture="$root/tests/external/tiny-aes-c/aes128_ecb_vectors.c"
headers="$root/tests/external/tiny-aes-c/include"
test "$(git -C "$root" hash-object "$aes/aes.c")" = 4481f7b24ec964019d38669842913fd571d28ba3
test "$(git -C "$root" hash-object "$aes/aes.h")" = b29b6683549632676ec11c06eb86efd02964db57
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc=riscv64-linux-gnu-gcc
flags=(-march=rv32i -mabi=ilp32 -DECB=1 -DCBC=0 -DCTR=0 -DMULTIPLY_AS_A_FUNCTION=1)
"$cc" "${flags[@]}" -E -P -nostdinc -x c -I"$headers" -I"$aes" \
  "$fixture" -o "$work/aes.i"
echo "RV32_TINYAES_INPUT=PASS source=untouched_AESECB128 fixture=original vendor=pinned"
if ! "$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/aes.i" -o "$work/aes.s" \
    >"$work/minic.out" 2>"$work/minic.err"; then
  test ! -e "$work/aes.s"
  echo "RV32_TINYAES=BLOCKED stage=MiniC/C0_Core_or_ILP32 original=true"
  sed -n '1,9p' "$work/minic.err"
  exit 0
fi
test -s "$work/aes.s"
echo "RV32_TINYAES_COMPILE=PASS source=original_C compiler=MiniC"
# Independent GNU C reference for THE SAME preprocessing result, ILP32 ABI,
# linked with THE SAME pinned GNU libgcc 14.2.0 soft arithmetic provider.
# A zero MiniC exit without a verified GNU result is not an acceptance.
"$cc" "${flags[@]}" -std=c11 -O0 -ffreestanding -fno-builtin \
  -x c -c "$work/aes.i" -o "$work/aes-gcc.o"
test -s "$work/aes-gcc.o"
if ! "$MINIAS" -march=rv32i -mabi=ilp32 \
    -o "$work/aes.o" "$work/aes.s" >"$work/as.out" 2>"$work/as.err"; then
  echo "RV32_TINYAES=BLOCKED stage=MiniAS/ELF32 original=true"
  sed -n '1,9p' "$work/as.err"
  exit 0
fi
cat >"$work/start.s" <<'SRC'
.text
.globl _start
_start:
    call main
    li a7, 93
    ecall
SRC
"$cc" "${flags[@]}" -x assembler -c "$work/start.s" -o "$work/start.o"
bash tests/compiler/c0/build-rv32-gcc-runtime.sh "$work"
riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
  -o "$work/aes-gcc.elf" "$work/start.o" "$work/aes-gcc.o" \
  "$work/libgcc-mulsi3.o" "$work/libgcc-divsi3.o"
if ! riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/aes.elf" "$work/start.o" "$work/aes.o" \
    "$work/libgcc-mulsi3.o" "$work/libgcc-divsi3.o" \
    >"$work/ld.out" 2>"$work/ld.err"; then
  echo "RV32_TINYAES=BLOCKED stage=GNU_LD/runtime_libcalls original=true"
  sed -n '1,12p' "$work/ld.err"
  exit 0
fi
set +e
timeout 45s qemu-riscv32 "$work/aes-gcc.elf" >"$work/gcc.out" 2>"$work/gcc.err"
gcc_code=$?
timeout 45s qemu-riscv32 "$work/aes.elf" >"$work/minic.out" 2>"$work/minic.err"
code=$?
set -e
if [[ "$gcc_code" != 0 || "$code" != 0 ]] ||
   ! cmp -s "$work/gcc.out" "$work/minic.out" ||
   ! cmp -s "$work/gcc.err" "$work/minic.err"; then
  echo "RV32_TINYAES=FAIL stage=QEMU_differential gcc_exit=$gcc_code minic_exit=$code" >&2
  sed -n '1,12p' "$work/minic.err" >&2
  exit 1
fi
echo "RV32_TINYAES_GNU_REFERENCE=PASS original_AES128_ECB_vectors=true compiler=GCC xlen=32"
echo "RV32_TINYAES=PASS original_AES128_ECB_vectors=true minic=1 minias=1 GNU_AS_reference=1 GNU_C_reference=1 GNU_LD=1 QEMU_byte_exact=1"
