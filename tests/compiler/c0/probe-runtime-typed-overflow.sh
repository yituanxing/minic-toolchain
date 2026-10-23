#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIC_CC:?set MINIC_CC to minic-cc}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/typed.i" <<'SRC'
int f0(int a,int b) { int r=0; return __builtin_sadd_overflow(a,b,&r); }
int f1(unsigned a,unsigned b) { unsigned r=0; return __builtin_uadd_overflow(a,b,&r); }
int f2(long a,long b) { long r=0; return __builtin_saddl_overflow(a,b,&r); }
int f3(unsigned long a,unsigned long b) { unsigned long r=0; return __builtin_uaddl_overflow(a,b,&r); }
int f4(long long a,long long b) { long long r=0; return __builtin_saddll_overflow(a,b,&r); }
int f5(unsigned long long a,unsigned long long b) { unsigned long long r=0; return __builtin_uaddll_overflow(a,b,&r); }
int f6(int a,int b) { int r=0; return __builtin_ssub_overflow(a,b,&r); }
int f7(unsigned a,unsigned b) { unsigned r=0; return __builtin_usub_overflow(a,b,&r); }
int f8(long a,long b) { long r=0; return __builtin_ssubl_overflow(a,b,&r); }
int f9(unsigned long a,unsigned long b) { unsigned long r=0; return __builtin_usubl_overflow(a,b,&r); }
int f10(long long a,long long b) { long long r=0; return __builtin_ssubll_overflow(a,b,&r); }
int f11(unsigned long long a,unsigned long long b) { unsigned long long r=0; return __builtin_usubll_overflow(a,b,&r); }
int f12(int a,int b) { int r=0; return __builtin_smul_overflow(a,b,&r); }
int f13(unsigned a,unsigned b) { unsigned r=0; return __builtin_umul_overflow(a,b,&r); }
int f14(long a,long b) { long r=0; return __builtin_smull_overflow(a,b,&r); }
int f15(unsigned long a,unsigned long b) { unsigned long r=0; return __builtin_umull_overflow(a,b,&r); }
int f16(long long a,long long b) { long long r=0; return __builtin_smulll_overflow(a,b,&r); }
int f17(unsigned long long a,unsigned long long b) { unsigned long long r=0; return __builtin_umulll_overflow(a,b,&r); }
SRC
for target in x86_64-linux aarch64-linux loongarch64-linux; do
  "$MINIC_CC" --target="$target" -S "$work/typed.i" -o "$work/$target.s"
  test -s "$work/$target.s"
  for id in $(seq 0 17); do
    grep -q "f${id}:" "$work/$target.s"
  done
  echo "RUNTIME_TYPED_OVERFLOW=PASS target=$target functions=18"
done
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$work/typed.i" -o "$work/riscv64-linux.s"
test -s "$work/riscv64-linux.s"
mkdir -p "$work/rv64i-only"
cp "$work/riscv64-linux.s" "$work/rv64i-only/"
python3 tools/ci/audit-rv64i-asm-origin.py --root "$work/rv64i-only" \
  --json-out "$work/report.json" --fail-on-compiler-non-i --expected-files 1 \
  --opcode-tsv-out "$work/opcode-counts.tsv"
echo 'RUNTIME_TYPED_OVERFLOW=PASS target=riscv64-linux functions=18 strict_I=1'
