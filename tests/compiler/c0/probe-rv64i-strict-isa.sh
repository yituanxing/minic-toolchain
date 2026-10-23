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

# RV64I emits standard integer compiler-runtime libcalls, with NO arithmetic
# algorithms maintained inside MiniC. RV64IM still selects native M.
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$tmp/mul.i" -o "$tmp/rv64i-fullarith.s"
test -s "$tmp/rv64i-fullarith.s"
for symbol in __muldi3 __divdi3 __moddi3; do
  grep -q "call $symbol" "$tmp/rv64i-fullarith.s"
done
if grep -Eq '^[[:space:]]*(mul|mulh|div|divu|rem|remu)[[:space:]]' "$tmp/rv64i-fullarith.s"; then
  echo "RV64_ISA=FAIL rv64i arithmetic leaked M" >&2; exit 1
fi
grep -Eq '^[[:space:]]*div[[:space:]]' "$tmp/rv64im.s"
grep -Eq '^[[:space:]]*rem[[:space:]]' "$tmp/rv64im.s"
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$tmp/mulonly.i" -o "$tmp/rv64i-mul.s"
test -s "$tmp/rv64i-mul.s"
grep -q 'call __muldi3' "$tmp/rv64i-mul.s"
if grep -Eq '^[[:space:]]*(mul|mulh|div|rem)[[:space:]]' "$tmp/rv64i-mul.s"; then
  echo 'RV64_ISA=FAIL RV64I software multiply leaked M' >&2; exit 1
fi
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
grep -q 'call __muldi3' "$tmp/oddptr-i.s"
# The pointer base must be RELOADED after the call, because t0 is
# caller-clobbered, and the compiler must preserve the function entry ra.
grep -Eq '[[:space:]]+sd ra,' "$tmp/oddptr-i.s"
grep -Eq '[[:space:]]+ld ra,' "$tmp/oddptr-i.s"
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
  # GCC libgcc 14.2.0 upstream RISC-V runtime sources, pinned unchanged
  # in tests/third_party (GPL-3.0 with GCC Runtime Library Exception 3.1).
  # These are TEST LINK PROVIDERS only, not MiniC-owned arithmetic code.
  runtime_src=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../third_party/gcc-riscv-14.2.0" && pwd)
  for kind in muldi3 div; do
    riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 \
      -I "$runtime_src" -c "$runtime_src/$kind.S" -o "$tmp/libgcc-$kind.o"
  done
  for selection in "rv64i lp64 add" "rv64im lp64 mul" "rv64imafd lp64d float"; do
    read -r march mabi source <<<"$selection"
    riscv64-linux-gnu-gcc "-march=$march" "-mabi=$mabi" \
      -c "$tmp/$march.s" -o "$tmp/$march.o"
    test -s "$tmp/$march.o"
  done
  for testasm in rv64i-mul rv64i-fullarith longptr-i oddptr-i; do
    riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 \
      -c "$tmp/$testasm.s" -o "$tmp/$testasm.o"
    test -s "$tmp/$testasm.o"
  done
  echo 'RV64_ISA_GNU_AS=PASS profiles=3 rv64i_mul_div_rem=1 pointer_scales=2'
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
          "$tmp/start.s" "$input" "$tmp/libgcc-muldi3.o" "$tmp/libgcc-div.o" \
          -o "$tmp/$compiler-$march.elf"
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
        "$tmp/softstart.s" "$tmp/$source.s" \
          "$tmp/libgcc-muldi3.o" "$tmp/libgcc-div.o" -o "$tmp/software.elf"
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
    cat >"$tmp/arithstart.s" <<'SRC'
.section .text
.globl _start
_start:
  li a0, 9
  li a1, 4
  call evaluate
  li a7, 93
  ecall
SRC
    riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 \
      -nostdlib -static -no-pie -Wl,-e,_start \
      "$tmp/arithstart.s" "$tmp/rv64i-fullarith.s" \
      "$tmp/libgcc-muldi3.o" "$tmp/libgcc-div.o" -o "$tmp/rv64i-fullarith.elf"
    set +e
    qemu-riscv64 "$tmp/rv64i-fullarith.elf" >"$tmp/fullarith.out" 2>"$tmp/fullarith.err"
    rc=$?
    set -e
    if (( rc != 37 )); then
      echo "RV64_ARITH_QEMU=FAIL combined mul/div/rem rc=$rc expected=37" >&2
      cat "$tmp/fullarith.err" >&2
      exit 1
    fi
    for variant in sdiv srem udiv urem sdiv32 srem32 udiv32 urem32; do
      case "$variant" in
        sdiv)   code='long evaluate(long a,long b) {return a/b;}' ;;
        srem)   code='long evaluate(long a,long b) {return a%b;}' ;;
        udiv)   code='unsigned long evaluate(unsigned long a,unsigned long b) {return a/b;}' ;;
        urem)   code='unsigned long evaluate(unsigned long a,unsigned long b) {return a%b;}' ;;
        sdiv32) code='int evaluate(int a,int b) {return a/b;}' ;;
        srem32) code='int evaluate(int a,int b) {return a%b;}' ;;
        udiv32) code='unsigned evaluate(unsigned a,unsigned b) {return a/b;}' ;;
        urem32) code='unsigned evaluate(unsigned a,unsigned b) {return a%b;}' ;;
      esac
      printf '%s\n' "$code" >"$tmp/$variant.i"
      for march in rv64i rv64im; do
        "$MINIC_CC" --target=riscv64-linux "-march=$march" -mabi=lp64 \
          -S "$tmp/$variant.i" -o "$tmp/minic-$variant-$march.s"
        riscv64-linux-gnu-gcc "-march=$march" -mabi=lp64 \
          -c "$tmp/minic-$variant-$march.s" -o "$tmp/minic-$variant-$march.o"
      done
      riscv64-linux-gnu-gcc -march=rv64im -mabi=lp64 \
        -O0 -ffreestanding -fno-pic -fno-pie -S \
        "$tmp/$variant.i" -o "$tmp/gnu-$variant-rv64im.s"
    done
    # Real ELF runtime differential against the hardware-M GNU oracle.
    # Exit status observes the low eight bits; signed/unsigned 32/64 cases
    # include a highest-bit dividend and positive/negative divisors.
    for sample in \
        "sdiv -37 4 247" "sdiv -37 -4 9" "sdiv 37 -4 247" \
        "srem -37 4 255" "srem -37 -4 255" "srem 37 -4 1" \
        "udiv -1 3 85" "urem -1 3 0" \
        "sdiv32 -37 4 247" "srem32 -37 4 255" \
        "udiv32 -1 3 85" "urem32 -1 3 0" \
        "udiv32 -1 7 36" "urem32 -1 7 3"; do
      read -r variant left right expected <<<"$sample"
      cat >"$tmp/divstart.s" <<SRC
.section .text
.globl _start
_start:
  li a0, $left
  li a1, $right
  call evaluate
  li a7, 93
  ecall
SRC
      for profile in minic-rv64i minic-rv64im gnu-rv64im; do
        IFS=- read -r compiler march <<<"$profile"
        source="$tmp/$compiler-$variant-$march.s"
        riscv64-linux-gnu-gcc "-march=$march" -mabi=lp64 \
          -nostdlib -static -no-pie -Wl,-e,_start \
          "$tmp/divstart.s" "$source" \
          "$tmp/libgcc-muldi3.o" "$tmp/libgcc-div.o" -o "$tmp/$profile.elf"
        set +e
        qemu-riscv64 "$tmp/$profile.elf" >"$tmp/$profile.out" 2>"$tmp/$profile.err"
        rc=$?
        set -e
        if (( rc != expected )); then
          echo "RV64_DIVREM_QEMU=FAIL variant=$variant a=$left b=$right profile=$profile rc=$rc expected=$expected" >&2
          cat "$tmp/$profile.err" >&2
          exit 1
        fi
      done
    done
    echo 'RV64_DIVREM_QEMU=PASS cases=14 profiles=3 signed_unsigned_32_64=1 gcc_oracle=1 upstream_libgcc=1'
    echo 'RV64_ISA_QEMU=PASS profiles=rv64i,rv64im gnu_minic_exit_exact=1'
  fi
fi
echo 'RV64_ISA_PROFILES=PASS rv64i_lp64=1 rv64im_lp64=1 rv64imafd_lp64d=1 runtime_libcalls=5 pointer_scale=1 rejected_isa_abi=1 default_byte_exact=1'
