#!/usr/bin/env bash
# Independently execute exactly the same FP IEEE-754 corpus against the
# reference LLVM-clang runtime and the MiniC-built runtime under RV64I LP64.
# Values are bit-checked, not merely compared to a second runtime implementation.
set -Eeuo pipefail
if (( $# != 2 )); then
  echo "usage: $0 /path/to/reference/libminic-rt.a /path/to/minic/libminic-rt.a" >&2
  exit 64
fi
reference="$1"
minic_rt="$2"
test -s "$reference"
test -s "$minic_rt"
command -v riscv64-linux-gnu-gcc >/dev/null
command -v qemu-riscv64 >/dev/null
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/test.c" <<'EOF'
typedef unsigned int u32;
typedef unsigned long u64;
union F { float f; u32 u; };
union D { double f; u64 u; };
extern float __addsf3(float,float), __subsf3(float,float);
extern float __mulsf3(float,float), __divsf3(float,float);
extern double __adddf3(double,double), __subdf3(double,double);
extern double __muldf3(double,double), __divdf3(double,double);
static float f(u32 v) { union F x; x.u=v; return x.f; }
static double d(u64 v) { union D x; x.u=v; return x.f; }
static u32 fb(float x) { union F v; v.f=x; return v.u; }
static u64 db(double x) { union D v; v.f=x; return v.u; }
static volatile u32 seedf=0x3f800000U; /* +1.0 */
static volatile u64 seedd=0x3ff0000000000000UL; /* +1.0 */
static int run(void) {
    const float one=f(seedf), two=f(0x40000000U), half=f(0x3f000000U);
    const double done=d(seedd), dtwo=d(0x4000000000000000UL);
    const double dhalf=d(0x3fe0000000000000UL);
    /* Cover four SF and four DF implementations with no driver FP arithmetic.
     * All operands are dynamically materialized IEEE payloads. */
    if (fb(__addsf3(one,two)) != 0x40400000U) return 1;
    if (fb(__subsf3(two,half)) != 0x3fc00000U) return 2;
    if (fb(__mulsf3(two,half)) != 0x3f800000U) return 3;
    if (fb(__divsf3(one,two)) != 0x3f000000U) return 4;
    if (db(__adddf3(done,dtwo)) != 0x4008000000000000UL) return 5;
    if (db(__subdf3(dtwo,dhalf)) != 0x3ff8000000000000UL) return 6;
    if (db(__muldf3(dtwo,dhalf)) != 0x3ff0000000000000UL) return 7;
    if (db(__divdf3(done,dtwo)) != 0x3fe0000000000000UL) return 8;
    if (fb(__mulsf3(f(0x80000000U), two)) != 0x80000000U) return 9;
    if (fb(__divsf3(one, f(0U))) != 0x7f800000U) return 10;
    if (db(__muldf3(d(0x8000000000000000UL),dtwo)) != 0x8000000000000000UL) return 11;
    if (db(__divdf3(done,d(0UL))) != 0x7ff0000000000000UL) return 12;
    /* Subnormal bit-carry, rounding and signed-zero transport. */
    if (fb(__addsf3(f(1U),f(1U))) != 2U) return 13;
    if (db(__adddf3(d(1UL),d(1UL))) != 2UL) return 14;
    if (fb(__addsf3(f(0x80000000U),f(0U))) != 0U) return 15;
    if (db(__adddf3(d(0x8000000000000000UL),d(0UL))) != 0UL) return 16;
    return 0;
}
int main(void) { return run(); }
EOF
cat >"$work/start.S" <<'EOF'
  .text
  .global _start
_start:
  call main
  li a7, 93
  ecall
EOF
riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -ffreestanding \
  -fno-builtin -fno-stack-protector -O1 -c "$work/test.c" -o "$work/test.o"
riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 \
  -c "$work/start.S" -o "$work/start.o"
for mode in reference minic; do
  if [[ "$mode" == reference ]]; then lib="$reference"; else lib="$minic_rt"; fi
  riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -static -nostdlib \
    -no-pie -Wl,-e,_start "$work/start.o" "$work/test.o" \
    "$lib" -o "$work/$mode.elf"
  set +e
  qemu-riscv64 "$work/$mode.elf"
  result=$?
  set -e
  if (( result != 0 )); then
    echo "RV64I_SOFTFLOAT_QEMU=FAIL mode=$mode case=$result" >&2
    exit 1
  fi
  echo "RV64I_SOFTFLOAT_QEMU=PASS mode=$mode cases=16 profile=rv64i-lp64"
done
