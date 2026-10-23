#!/usr/bin/env bash
# Reuse the existing MiniC build; GNU AS/QEMU checks run only when tools exist.
set -Eeuo pipefail
: "$MINIC_CC"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cat >"$tmp/add.i" <<'SRC'
long evaluate(long a, long b) { return a + b; }
SRC
cat >"$tmp/mul.i" <<'SRC'
long evaluate(long a, long b) { return a * b + a / b - a % b; }
SRC
cat >"$tmp/mulonly.i" <<'SRC'
long evaluate(long a, long b) { return a * b; }
SRC
cat >"$tmp/float.i" <<'SRC'
double addf(double a, double b) { return a + b; }
SRC

"$MINIC_CC" --target=riscv64-linux -S "$tmp/add.i" -o "$tmp/default.s"
"$MINIC_CC" --target=riscv64-linux -mabi=lp64d -S "$tmp/add.i" -o "$tmp/default-explicit.s"
cmp "$tmp/default.s" "$tmp/default-explicit.s"
for selection in "rv64i lp64 add" "rv64im lp64 mul" "rv64imafd lp64d float"; do
  read -r march mabi source <<<"$selection"
  "$MINIC_CC" --target=riscv64-linux "-march=$march" "-mabi=$mabi" \
    -S "$tmp/$source.i" -o "$tmp/$march.s"
  test -s "$tmp/$march.s"
done

# RV64I now lowers 64-bit multiply to proven shift/add instructions;
# division and remainder still fail explicitly without M.
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$tmp/mulonly.i" -o "$tmp/rv64i-mul.s"
test -s "$tmp/rv64i-mul.s"
grep -q '1:' "$tmp/rv64i-mul.s"
if grep -Eq '^[[:space:]]*(mul|mulh|div|rem)[[:space:]]' "$tmp/rv64i-mul.s"; then
  echo 'RV64_ISA=FAIL RV64I software multiply leaked M' >&2; exit 1
fi
if "$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$tmp/mul.i" -o "$tmp/invalid-div.s" >"$tmp/mul.stdout" 2>"$tmp/mul.stderr"; then
  echo 'RV64_ISA=FAIL rv64i native divide admitted' >&2; exit 1
fi
grep -Eq "instruction '(div|rem)' requires M" "$tmp/mul.stderr"
test ! -e "$tmp/invalid-div.s"
for march in rv64i rv64im; do
  if "$MINIC_CC" --target=riscv64-linux "-march=$march" -mabi=lp64 \
      -S "$tmp/float.i" -o "$tmp/invalid-float.s" >"$tmp/fp.out" 2>"$tmp/fp.err"; then
    echo "RV64_ISA=FAIL $march accepted FP" >&2; exit 1
  fi
  grep -Eq 'requires F(/D)?|LP64 soft-float ABI' "$tmp/fp.err"
  test ! -e "$tmp/invalid-float.s"
done
for selection in "rv64i lp64d" "rv64im lp64d" "rv64imafd lp64"; do
  read -r march mabi <<<"$selection"
  if "$MINIC_CC" --target=riscv64-linux "-march=$march" "-mabi=$mabi" \
      -S "$tmp/add.i" -o "$tmp/invalid-abi.s" >"$tmp/abi.out" 2>"$tmp/abi.err"; then
    echo "RV64_ISA=FAIL $selection accepted" >&2; exit 1
  fi
  grep -q 'ISA and ABI mismatch' "$tmp/abi.err"
done
for unqualified in rv64gc rv64imac rv32i rv64imaf; do
  if "$MINIC_CC" --target=riscv64-linux "-march=$unqualified" -mabi=lp64 \
      -S "$tmp/add.i" -o "$tmp/unqualified.s" >"$tmp/bad.out" 2>"$tmp/bad.err"; then
    echo "RV64_ISA=FAIL admitted $unqualified" >&2; exit 1
  fi
  grep -q 'supported explicit -march' "$tmp/bad.err"
done

# ISA selection is made over reachable blocks, including implicit pointer
# scaling and overflow helpers, not merely the literal C multiply operator.
cat >"$tmp/charptr.i" <<'SRC'
char *advance(char *p, long offset) { return p + offset; }
SRC
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$tmp/charptr.i" -o "$tmp/charptr.s"
test -s "$tmp/charptr.s"

cat >"$tmp/longptr.i" <<'SRC'
long *advance(long *p, long offset) { return p + offset; }
SRC
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$tmp/longptr.i" -o "$tmp/longptr-i.s"
grep -q 'slli t1, t1, 3' "$tmp/longptr-i.s"
if grep -Eq '^[[:space:]]*mul[[:space:]]' "$tmp/longptr-i.s"; then
  echo 'RV64_ISA=FAIL pointer scale emitted hardware M' >&2; exit 1
fi
cat >"$tmp/oddptr.i" <<'SRC'
struct Packed3 { char bytes[3]; };
struct Packed3 *evaluate(struct Packed3 *p, long n) { return p + n; }
SRC
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$tmp/oddptr.i" -o "$tmp/oddptr-i.s"
grep -q '1:' "$tmp/oddptr-i.s"
if grep -Eq '^[[:space:]]*mul[[:space:]]' "$tmp/oddptr-i.s"; then
  echo 'RV64_ISA=FAIL odd-size pointer scale emitted hardware M' >&2; exit 1
fi
"$MINIC_CC" --target=riscv64-linux -march=rv64im -mabi=lp64 \
  -S "$tmp/longptr.i" -o "$tmp/longptr-im.s"
test -s "$tmp/longptr-im.s"

# A plain double passthrough has NO FP arithmetic but the existing RV64
# psABI uses fa0; it is *not* a correct LP64 soft-float ABI implementation.
# The compiler must fail closed rather than successfully emit the wrong ABI.
cat >"$tmp/fpabi.i" <<'SRC'
double passthrough(double value) { return value; }
SRC
for march in rv64i rv64im; do
  if "$MINIC_CC" --target=riscv64-linux "-march=$march" -mabi=lp64 \
     -S "$tmp/fpabi.i" -o "$tmp/invalid-fpabi.s" \
     >"$tmp/fpabi.out" 2>"$tmp/fpabi.err"; then
    echo "RV64_ISA=FAIL $march admitted wrong float calling ABI" >&2; exit 1
  fi
  grep -q 'LP64 soft-float ABI' "$tmp/fpabi.err"
  test ! -e "$tmp/invalid-fpabi.s"
done
cat >"$tmp/record-fpabi.i" <<'SRC'
struct Box { double value; };
struct Box passthrough(struct Box box) { return box; }
SRC
if "$MINIC_CC" --target=riscv64-linux -march=rv64im -mabi=lp64 \
  -S "$tmp/record-fpabi.i" -o "$tmp/invalid-record-fpabi.s" \
  >"$tmp/record-fpabi.out" 2>"$tmp/record-fpabi.err"; then
  echo "RV64_ISA=FAIL admitted FP aggregate ABI" >&2; exit 1
fi
grep -q 'LP64 soft-float ABI' "$tmp/record-fpabi.err"
test ! -e "$tmp/invalid-record-fpabi.s"

# The GNU assembler is an independent legality oracle for each exact profile.
if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
  for selection in "rv64i lp64 add" "rv64im lp64 mul" "rv64imafd lp64d float"; do
    read -r march mabi source <<<"$selection"
    riscv64-linux-gnu-gcc "-march=$march" "-mabi=$mabi" \
      -c "$tmp/$march.s" -o "$tmp/$march.o"
    test -s "$tmp/$march.o"
  done
  for testasm in rv64i-mul longptr-i oddptr-i; do
    riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 \
      -c "$tmp/$testasm.s" -o "$tmp/$testasm.o"
    test -s "$tmp/$testasm.o"
  done
  echo 'RV64_ISA_GNU_AS=PASS profiles=3 soft_multiply=1 pointer_scale_pow2=1 pointer_scale_odd=1'
  if command -v qemu-riscv64 >/dev/null 2>&1; then
    cat >"$tmp/start.s" <<'SRC'
.section .text
.globl _start
_start:
  li a0, 9
  li a1, 4
  call evaluate
  li a7, 93
  ecall
SRC
    for selection in "rv64i lp64 add 13" "rv64im lp64 mul 37"; do
      read -r march mabi source expected <<<"$selection"
      riscv64-linux-gnu-gcc "-march=$march" "-mabi=$mabi" \
        -O0 -ffreestanding -fno-pic -fno-pie -S \
        "$tmp/$source.i" -o "$tmp/gnu-$march.s"
      for compiler in gnu minic; do
        input="$tmp/$march.s"
        if [[ "$compiler" == gnu ]]; then input="$tmp/gnu-$march.s"; fi
        riscv64-linux-gnu-gcc "-march=$march" "-mabi=$mabi" \
          -nostdlib -static -no-pie -Wl,-e,_start \
          "$tmp/start.s" "$input" -o "$tmp/$compiler-$march.elf"
        set +e
        qemu-riscv64 "$tmp/$compiler-$march.elf" \
          >"$tmp/$compiler-$march.out" 2>"$tmp/$compiler-$march.err"
        rc=$?
        set -e
        if (( rc != expected )); then
          echo "RV64_ISA_QEMU=FAIL compiler=$compiler march=$march rc=$rc expected=$expected" >&2
          cat "$tmp/$compiler-$march.err" >&2
          exit 1
        fi
      done
      cmp "$tmp/gnu-$march.out" "$tmp/minic-$march.out"
    done
    # The linker, ELF attributes, actual assembly and QEMU execution are
    # all explicitly RV64I even for negative signed multiplication.
    for selection in "rv64i-mul 9 4 36" "rv64i-mul -7 11 179" \
                     "rv64i-mul 0 -1 0" "oddptr-i 1000 7 253"; do
      read -r source a b expected <<<"$selection"
      cat >"$tmp/softstart.s" <<SRC
.section .text
.globl _start
_start:
  li a0, $a
  li a1, $b
  call evaluate
  li a7, 93
  ecall
SRC
      riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 \
        -nostdlib -static -no-pie -Wl,-e,_start \
        "$tmp/softstart.s" "$tmp/$source.s" -o "$tmp/software.elf"
      set +e
      qemu-riscv64 "$tmp/software.elf" >"$tmp/soft.out" 2>"$tmp/soft.err"
      rc=$?
      set -e
      if (( rc != expected )); then
        echo "RV64_SOFTMUL_QEMU=FAIL source=$source a=$a b=$b rc=$rc expected=$expected" >&2
        cat "$tmp/soft.err" >&2
        exit 1
      fi
    done
    echo 'RV64_SOFTMUL_QEMU=PASS signed_negative=1 zero=1 odd_pointer=1'
    echo 'RV64_ISA_QEMU=PASS profiles=rv64i,rv64im gnu_minic_exit_exact=1'
  fi
fi
echo 'RV64_ISA_PROFILES=PASS rv64i_lp64=1 rv64im_lp64=1 rv64imafd_lp64d=1 software_mul=1 pointer_scale=1 rejected_isa_abi=1 default_byte_exact=1'
