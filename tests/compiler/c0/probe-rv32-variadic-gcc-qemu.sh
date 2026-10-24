#!/usr/bin/env bash
# Independent RV32I/ILP32 GNU C/MiniC variadic callee differential.
# Test named-register and stack overflow, mutable va_list, va_copy,
# and pointer forwarding into the authentic Picolibc vsnprintf.
set -Eeuo pipefail
: "${MINIC_CC:?}"
: "${MINIAS:=${MINIC_CC%/minic-cc}/minic-as}"
: "${BARE_CC:=riscv64-unknown-elf-gcc}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
root=$(git rev-parse --show-toplevel)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
target=(-march=rv32i -mabi=ilp32)
libc=(-specs=picolibc.specs)
cat >"$work/varargs.c" <<'SRC'
#include <stdarg.h>
#include <stdio.h>
int vformat(char *dst, const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(dst, 180, fmt, ap);
    va_end(ap);
    return n;
}
int vsum(int fixed, ...) {
    va_list ap, cp;
    int i, total = fixed;
    va_start(ap, fixed);
    va_copy(cp, ap);
    for (i = 0; i < 9; ++i) total += va_arg(cp, int);
    va_end(cp);
    va_end(ap);
    return total;
}
int vspilled(int a,int b,int c,int d,int e,int f,int g,int h, ...) {
    va_list ap;
    int x, y;
    va_start(ap, h);
    x = va_arg(ap, int);
    y = va_arg(ap, int);
    va_end(ap);
    return a+b+c+d+e+f+g+h+x+y;
}
SRC
cat >"$work/driver.c" <<'SRC'
#include <string.h>
extern int vformat(char *, const char *, ...);
extern int vsum(int,...);
extern int vspilled(int,int,int,int,int,int,int,int,...);
int main(void) {
    char dst[180];
    if (vformat(dst,"%d %s %ld %u",37,"RV32",777777L,41U)!=17)
        return 1;
    if (strcmp(dst,"37 RV32 777777 41")!=0) return 2;
    if (vsum(10,1,2,3,4,5,6,7,8,9)!=55) return 3;
    if (vspilled(1,2,3,4,5,6,7,8,9,10)!=55) return 4;
    return 0;
}
SRC
"$BARE_CC" "${target[@]}" "${libc[@]}" -E -P -std=c11 \
  "$work/varargs.c" -o "$work/varargs.i"
"$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
  -x c -c "$work/varargs.i" -o "$work/reference.o"
"$MINIC_CC" --target=riscv32-linux "${target[@]}" -S \
  "$work/varargs.i" -o "$work/minic.s"
"$RISCV_CC" "${target[@]}" -c "$work/minic.s" -o "$work/gnu-as.o"
"$MINIAS" "${target[@]}" -o "$work/minias.o" "$work/minic.s"
"$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
  -c "$work/driver.c" -o "$work/driver.o"
for name in rv32-linux-picolibc-start.S rv32-linux-picolibc-syscalls.c; do
  stem="${name%.*}"
  "$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding \
    -c "$root/tests/compiler/c0/$name" -o "$work/$stem.o"
done
picolibc=$(find /usr/lib/picolibc -type f -path '*/rv32i/ilp32/libc.a' -print -quit)
picomath=$(find /usr/lib/picolibc -type f -path '*/rv32i/ilp32/libm.a' -print -quit)
baregcc=$("$BARE_CC" "${target[@]}" -print-libgcc-file-name)
for mode in reference gnu-as minias; do
  riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/$mode.elf" "$work/rv32-linux-picolibc-start.o" \
    "$work/rv32-linux-picolibc-syscalls.o" "$work/driver.o" \
    "$work/$mode.o" --start-group "$picomath" "$picolibc" "$baregcc" --end-group
  set +e
  timeout 30s qemu-riscv32 "$work/$mode.elf"
  rc=$?
  set -e
  if ((rc != 0)); then
    echo "RV32_VARIADIC=FAIL mode=$mode exit=$rc" >&2
    exit 1
  fi
  echo "RV32_VARIADIC_MODE=PASS mode=$mode"
done
echo 'RV32_VARIADIC=PASS GCC=1 MiniC=1 GNU_AS=1 MiniAS=1 ELF32=1 QEMU=1 va_start=1 va_copy=1 va_arg_int=1 vsnprintf_forward=1 stack_overflow=1'
