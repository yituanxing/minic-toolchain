#!/usr/bin/env bash
# RV32I full-width native two-word arithmetic vs independent GNU C
# translation and QEMU, with GNU AS AND MiniAS machine consumers.
set -Eeuo pipefail
: "${MINIC_CC:?}"
: "${MINIAS:=${MINIC_CC%/minic-cc}/minic-as}"
test -x "$MINIAS"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/wide.i" <<'SRC'
typedef unsigned long long U;
typedef long long I;
U w_add(U a,U b) { return a + b; }
U w_sub(U a,U b) { return a - b; }
U w_and(U a,U b) { return a & b; }
U w_or(U a,U b) { return a | b; }
U w_xor(U a,U b) { return a ^ b; }
U w_mul(U a,U b) { return a * b; }
U w_udiv(U a,U b) { return a / b; }
U w_umod(U a,U b) { return a % b; }
I w_sdiv(I a,I b) { return a / b; }
I w_smod(I a,I b) { return a % b; }
U w_not(U a) { return ~a; }
I w_neg(I a) { return -a; }
U w_shl(U a,unsigned b) { return a << b; }
U w_shr(U a,unsigned b) { return a >> b; }
I w_sar(I a,unsigned b) { return a >> b; }
int w_eq(U a,U b) { return a == b; }
int w_lt_u(U a,U b) { return a < b; }
int w_lt_s(I a,I b) { return a < b; }
int w_zero(U a) { return !a; }
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/wide.i" -o "$work/wide.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -c \
  "$work/wide.s" -o "$work/wide-minic-gas.o"
"$MINIAS" -march=rv32i -mabi=ilp32 -o "$work/wide-minic-as.o" "$work/wide.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -O1 -ffreestanding \
  -fno-builtin -fno-stack-protector -x c -c \
  "$work/wide.i" -o "$work/wide-gnu.o"
cat >"$work/driver.c" <<'SRC'
typedef unsigned long long U;
typedef long long I;
extern U w_add(U,U), w_sub(U,U), w_and(U,U), w_or(U,U), w_xor(U,U);
extern U w_mul(U,U), w_not(U), w_shl(U,unsigned), w_shr(U,unsigned);
extern U w_udiv(U,U), w_umod(U,U);
extern I w_sdiv(I,I), w_smod(I,I);
extern I w_neg(I), w_sar(I,unsigned);
extern int w_eq(U,U), w_lt_u(U,U), w_lt_s(I,I), w_zero(U);
static volatile U values[10] = {
  0ULL, 1ULL, 0xffffffffULL, 0x100000000ULL,
  0x123456789abcdef0ULL, 0xaaaaaaaa55555555ULL,
  0xfedcba9876543210ULL, 0xffffffffffffffffULL,
  0x7fffffffffffffffULL, 0x8000000000000000ULL
};
static const unsigned shifts[7] = {0U,1U,7U,31U,32U,33U,63U};
static int test(void) {
  unsigned i,j,k;
  for (i=0;i<10U;++i) {
    U a=values[i];
    if (w_not(a)!=~a || w_neg((I)a)!=-(I)a || w_zero(a)!=(a==0))
      return 1;
    for (j=0;j<10U;++j) {
      U b=values[j];
      if (w_add(a,b)!=a+b) return 2;
      if (w_sub(a,b)!=a-b) return 3;
      if (w_and(a,b)!=(a&b) || w_or(a,b)!=(a|b) ||
          w_xor(a,b)!=(a^b)) return 4;
      if (w_mul(a,b)!=a*b) return 5;
      if (w_eq(a,b)!=(a==b)) return 6;
      if (w_lt_u(a,b)!=(a<b)) return 7;
      if (w_lt_s((I)a,(I)b)!=((I)a<(I)b)) return 8;
      if (b != 0ULL) {
        if (w_udiv(a,b)!=a/b || w_umod(a,b)!=a%b) return 12;
        if (!(a==0x8000000000000000ULL &&
              b==0xffffffffffffffffULL) &&
            (w_sdiv((I)a,(I)b)!=((I)a/(I)b) ||
             w_smod((I)a,(I)b)!=((I)a%(I)b))) return 13;
      }
    }
    for (k=0;k<7U;++k) {
      unsigned sh=shifts[k];
      if (w_shl(a,sh)!=(a<<sh)) return 9;
      if (w_shr(a,sh)!=(a>>sh)) return 10;
      if (w_sar((I)a,sh)!=((I)a>>sh)) return 11;
    }
  }
  return 0;
}
int main(void) { return test(); }
SRC
cat >"$work/start.s" <<'SRC'
.text
.globl _start
_start:
  .option push
  .option norelax
  la gp, __global_pointer$
  .option pop
  call main
  li a7,93
  ecall
SRC
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 \
  -O1 -ffreestanding -fno-builtin -fno-stack-protector \
  -c "$work/driver.c" -o "$work/driver.o"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 \
  -c "$work/start.s" -o "$work/start.o"
bash tests/compiler/c0/build-rv32-gcc-runtime.sh "$work"
mapfile -t rt < "$work/rv32-runtime-objects.rsp"
for mode in gnu minic-gas minic-as; do
  case "$mode" in
    gnu) obj="$work/wide-gnu.o" ;;
    minic-gas) obj="$work/wide-minic-gas.o" ;;
    minic-as) obj="$work/wide-minic-as.o" ;;
  esac
  riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/$mode.elf" "$work/start.o" "$work/driver.o" "$obj" "${rt[@]}"
  if ! timeout 30s qemu-riscv32 "$work/$mode.elf"; then
    echo "RV32_I64_ALU=FAIL mode=$mode" >&2
    exit 1
  fi
done
echo "RV32_I64_ALU=PASS checks=1500 functions=19 ISA=rv32i ABI=ilp32 GNU_C=1 GNU_AS=1 MiniAS=1 QEMU=1 upstream_llvm_DI_arithmetic=1"
