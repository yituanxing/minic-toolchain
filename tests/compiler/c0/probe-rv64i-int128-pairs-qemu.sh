#!/usr/bin/env bash
# End-to-end RV64I/LP64 ABI differential acceptance for MiniC 128-bit integer
# pairs against pinned, reference-built upstream LLVM compiler-rt libcalls.
# No GNU C compiler produces the functions under test: it compiles only the
# independent test driver, and GNU AS assembles MiniC's .s.
set -Eeuo pipefail
if (( $# != 2 )); then
  echo "usage: $0 /path/to/minic-cc /path/to/reference/libminic-rt.a" >&2
  exit 64
fi
minic="$1"
rt="$2"
test -s "$rt"
for cmd in riscv64-linux-gnu-gcc qemu-riscv64; do
  command -v "$cmd" >/dev/null || { echo "missing mandatory tool $cmd" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/arith.i" <<'EOF'
typedef unsigned __int128 U;
typedef __int128 I;
U ti_add(U a, U b) { return a + b; }
U ti_sub(U a, U b) { return a - b; }
U ti_and(U a, U b) { return a & b; }
U ti_or(U a, U b) { return a | b; }
U ti_xor(U a, U b) { return a ^ b; }
U ti_not(U a) { return ~a; }
I ti_neg(I a) { return -a; }
U ti_shl(U a, unsigned b) { return a << b; }
U ti_shr(U a, unsigned b) { return a >> b; }
I ti_sar(I a, unsigned b) { return a >> b; }
I ti_div(I a, I b) { return a / b; }
I ti_mod(I a, I b) { return a % b; }
U ti_udiv(U a, U b) { return a / b; }
U ti_umod(U a, U b) { return a % b; }
EOF
"$minic" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$work/arith.i" -o "$work/arith.s"
riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -c \
  "$work/arith.s" -o "$work/arith.o"
cat >"$work/driver.c" <<'EOF'
typedef unsigned long u64;
typedef unsigned __int128 U;
typedef __int128 I;
union WordPair { U x; struct { u64 lo, hi; } s; };
extern U ti_add(U,U), ti_sub(U,U), ti_and(U,U), ti_or(U,U), ti_xor(U,U);
extern U ti_not(U), ti_shl(U,unsigned), ti_shr(U,unsigned);
extern I ti_neg(I), ti_sar(I,unsigned), ti_div(I,I), ti_mod(I,I);
extern U ti_udiv(U,U), ti_umod(U,U);
static U pair(u64 hi, u64 lo) {
  union WordPair p;
  p.s.hi = hi;
  p.s.lo = lo;
  return p.x;
}
static int match(U value, u64 hi, u64 lo) {
  union WordPair p; p.x=value;
  return p.s.hi == hi && p.s.lo == lo;
}
#define ALL (~(u64)0)
int run_tests(void) {
  const U a = pair(0x123456789abcdef0UL, 0xfedcba9876543210UL);
  const U b = pair(0x0f0f0f0f0f0f0f0fUL, 0xf0f0f0f0f0f0f0f0UL);
  if (!match(ti_add(pair(1,ALL),pair(0,1)),2,0)) return 1;
  if (!match(ti_sub(pair(1,0),pair(0,1)),0,ALL)) return 2;
  if (!match(ti_and(a,b),0x020406080a0c0e00UL,0xf0d0b09070503010UL)) return 3;
  if (!match(ti_or(a,b),0x1f3f5f7f9fbfdfffUL,0xfefcfaf8f6f4f2f0UL)) return 4;
  if (!match(ti_xor(a,b),0x1d3b597795b3d1ffUL,0x0e2c4a6886a4c2e0UL)) return 5;
  if (!match(ti_not(pair(0,0)),ALL,ALL)) return 6;
  if (!match((U)ti_neg((I)pair(0,1)),ALL,ALL)) return 7;
  if (!match(ti_shl(pair(0,1),65),2,0)) return 8;
  if (!match(ti_shl(pair(0,1),127),1UL<<63,0)) return 9;
  if (!match(ti_shr(pair(1,0),64),0,1)) return 10;
  if (!match(ti_shr(pair(1,0),63),0,2)) return 11;
  if (!match((U)ti_sar((I)pair(ALL,0),64),ALL,ALL)) return 12;
  if (!match((U)ti_div(-256,7),ALL, (u64)-36)) return 13;
  if (!match((U)ti_mod(-256,7),ALL, (u64)-4)) return 14;
  if (!match(ti_udiv(pair(16,0),pair(1,0)),0,16)) return 15;
  if (!match(ti_umod(pair(16,3),pair(1,0)),0,3)) return 16;
  return 0;
}
EOF
cat >"$work/start.S" <<'EOF'
  .text
  .global _start
_start:
  call run_tests
  li a7, 93
  ecall
EOF
riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 \
  -ffreestanding -fno-stack-protector -fno-builtin -O1 -c \
  "$work/driver.c" -o "$work/driver.o"
riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 \
  -c "$work/start.S" -o "$work/start.o"
riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -static -nostdlib \
  -Wl,-e,_start -no-pie "$work/start.o" "$work/driver.o" \
  "$work/arith.o" "$rt" -o "$work/test.elf"
set +e
qemu-riscv64 "$work/test.elf"
rc=$?
set -e
if (( rc != 0 )); then
  echo "RV64I_I128_QEMU=FAIL case=$rc" >&2
  exit 1
fi
echo 'RV64I_I128_QEMU=PASS cases=16 MiniC_generated_pair_code=1 upstream_llvm_runtime=1'
