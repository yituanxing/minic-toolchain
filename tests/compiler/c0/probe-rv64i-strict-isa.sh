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

# A base-I ISA does not imply every GNU C extension is implemented.
# Explicit targets must never quietly truncate 128-bit arithmetic or use an
# incorrect one-register __int128 ABI merely because emitted opcodes are I.
for variant in div add shl equal; do
  case "$variant" in
    div) body='unsigned __int128 probe(void) { unsigned __int128 a = 33, b = 3; return a / b; }' ;;
    add) body='unsigned __int128 probe(void) { unsigned __int128 a = 33, b = 3; return a + b; }' ;;
    shl) body='unsigned __int128 probe(void) { unsigned __int128 a = 1; return a << 66; }' ;;
    equal) body='int probe(void) { unsigned __int128 a = 1, b = 2; return a == b; }' ;;
  esac
  printf '%s\n' "$body" >"$tmp/int128-$variant.i"
  if "$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$tmp/int128-$variant.i" -o "$tmp/invalid-int128-$variant.s" \
    >"$tmp/128.out" 2>"$tmp/128.err"; then
    echo "RV64I_INT128=FAIL accepted unsafe $variant" >&2; exit 1
  fi
  test ! -e "$tmp/invalid-int128-$variant.s"
done
cat >"$tmp/unsupported-i128-abi.i" <<'SRC'
unsigned __int128 passthrough(unsigned __int128 a) { return a; }
SRC
if "$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$tmp/unsupported-i128-abi.i" -o "$tmp/invalid-i128-abi.s" \
  >"$tmp/i128-abi.out" 2>"$tmp/i128-abi.err"; then
  echo 'RV64I_INT128=FAIL accepted unqualified two-register ABI' >&2; exit 1
fi
grep -q '__int128 parameter/return' "$tmp/i128-abi.err"
test ! -e "$tmp/invalid-i128-abi.s"


# First supported LP64 ABI contract is intentionally *scalar integer-only*.
# This includes pointers (carried as XLEN integers), and void results,
# but NOT by-value aggregates, varargs, floating signatures or __int128.
cat >"$tmp/strict-abi-integer.i" <<'SRC'
long pass(long value, unsigned count, const char *ptr) {
  return value + count + (ptr != 0);
}
SRC
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$tmp/strict-abi-integer.i" -o "$tmp/strict-abi-integer.s"
test -s "$tmp/strict-abi-integer.s"
for variant in aggregate variadic; do
  case "$variant" in
    aggregate) body='struct Pair { long x; long y; }; struct Pair take(struct Pair v) { return v; }' ;;
    variadic) body='int take(int a, ...) { return a; }' ;;
  esac
  printf '%s\n' "$body" >"$tmp/strict-abi-$variant.i"
  if "$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
       -S "$tmp/strict-abi-$variant.i" -o "$tmp/invalid-$variant.s" \
       >"$tmp/$variant.out" 2>"$tmp/$variant.err"; then
    echo "RV64_LP64_ABI=FAIL admitted $variant" >&2; exit 1
  fi
  grep -q 'LP64 integer-only ABI' "$tmp/$variant.err" || {
    cat "$tmp/$variant.err" >&2; exit 1;
  }
  test ! -e "$tmp/invalid-$variant.s"
done
cat >"$tmp/strict-abi-indirect.i" <<'SRC'
int invoke(double (*fn)(double), int x) { return (int)fn(x); }
SRC
if "$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
     -S "$tmp/strict-abi-indirect.i" -o "$tmp/invalid-indirect.s" \
     >"$tmp/indirect.out" 2>"$tmp/indirect.err"; then
  echo 'RV64_LP64_ABI=FAIL admitted indirect floating signature' >&2; exit 1
fi
grep -q 'LP64 integer-only ABI' "$tmp/indirect.err" || {
  cat "$tmp/indirect.err" >&2; exit 1;
}
test ! -e "$tmp/invalid-indirect.s"

# Non-ABI-exposed local float/double arithmetic: the function takes and
# returns integer bit patterns. All calculations use standard GCC soft-float
# symbols through RV64I integer argument registers, never F/D instructions.
cat >"$tmp/softfp-local.i" <<'SRC'
union D { double value; unsigned long bits; };
union F { float value; unsigned bits; };
unsigned long add_double_bits(unsigned long a, unsigned long b) {
  union D x, y, z;
  x.bits = a; y.bits = b;
  z.value = x.value + y.value;
  return z.bits;
}
unsigned long div_double_bits(unsigned long a, unsigned long b) {
  union D x, y, z;
  x.bits = a; y.bits = b;
  z.value = x.value / y.value;
  return z.bits;
}
unsigned add_float_bits(unsigned a, unsigned b) {
  union F x, y, z;
  x.bits = a; y.bits = b;
  z.value = x.value + y.value;
  return z.bits;
}
unsigned mul_float_bits(unsigned a, unsigned b) {
  union F x, y, z;
  x.bits = a; y.bits = b;
  z.value = x.value * y.value;
  return z.bits;
}
SRC
for march in rv64i rv64im; do
  "$MINIC_CC" --target=riscv64-linux "-march=$march" -mabi=lp64 \
    -S "$tmp/softfp-local.i" -o "$tmp/softfp-$march.s"
  for sym in __adddf3 __divdf3 __addsf3 __mulsf3; do
    grep -q "call $sym" "$tmp/softfp-$march.s" || {
      echo "RV64_SOFTFP=FAIL missing $sym in $march" >&2; exit 1;
    }
  done
  grep -Eq '[[:space:]]+sd ra,' "$tmp/softfp-$march.s"
  grep -Eq '[[:space:]]+ld ra,' "$tmp/softfp-$march.s"
  if grep -Eq '^[[:space:]]*(fadd|fsub|fmul|fdiv|fcvt|fmv)\.' "$tmp/softfp-$march.s"; then
    echo "RV64_SOFTFP=FAIL leaked F/D opcode in $march" >&2; exit 1
  fi
done
echo 'RV64_LP64_ABI=PASS scalar_integer_pointer_only=1 aggregates_varargs_indirect_fp_rejected=1'
echo 'RV64_SOFTFP_LIBCALL=PASS sf=2 df=2 interface_integer_only=1'
# The GNU assembler is an independent legality oracle for each exact profile.
if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
  # The same optional archived upstream runtime used by real MiniC links.
  # Build it once with matching ISA and ABI, not as ad-hoc test objects.
  repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
  runtime_build="$tmp/rtbuild"
  make -s -C "$repo_root" BUILD_DIR="$runtime_build" \
    RISCV_RUNTIME_CC=riscv64-linux-gnu-gcc \
    RISCV_RUNTIME_AR=riscv64-linux-gnu-ar \
    RISCV_RUNTIME_NM=riscv64-linux-gnu-nm \
    check-rv64i-runtime-archive
  runtime_lib="$runtime_build/runtime/riscv64/rv64i-lp64/libminic-rt.a"
  test -s "$runtime_lib"
  for selection in "rv64i lp64 add" "rv64im lp64 mul" "rv64imafd lp64d float"; do
    read -r march mabi source <<<"$selection"
    riscv64-linux-gnu-gcc "-march=$march" "-mabi=$mabi" \
      -c "$tmp/$march.s" -o "$tmp/$march.o"
    test -s "$tmp/$march.o"
  done
  for testasm in rv64i-mul rv64i-fullarith longptr-i oddptr-i softfp-rv64i; do
    riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 \
      -c "$tmp/$testasm.s" -o "$tmp/$testasm.o"
    test -s "$tmp/$testasm.o"
  done
  # The .s file has GNU assembler pseudos (li/mv/call/ret); only the
  # assembled instructions are the ISA contract. Decode REAL machine words
  # and ELF ISA/ABI attributes in addition to our pre-assembly text audit.
  command -v python3 >/dev/null
  command -v riscv64-linux-gnu-objdump >/dev/null
  command -v riscv64-linux-gnu-readelf >/dev/null
  machine_audit="$repo_root/tests/compiler/c0/audit-rv64i-machine.py"
  python3 "$machine_audit" --selftest
  python3 "$machine_audit" "$tmp/rv64i.o" "$tmp/rv64i-mul.o" \
    "$tmp/rv64i-fullarith.o" "$tmp/longptr-i.o" "$tmp/oddptr-i.o"
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
          "$tmp/start.s" "$input" "$runtime_lib" \
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
      # A pure-I add must not drag in libgcc builtins merely because an
      # archive was named on the final link command.
      if [[ "$march" == rv64i ]] &&
         riscv64-linux-gnu-nm -g "$tmp/minic-$march.elf" | grep -q __muldi3; then
        echo "RV64_RUNTIME_ARCHIVE=FAIL unnecessary_builtin_extraction" >&2; exit 1
      fi
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
          "$runtime_lib" -o "$tmp/software.elf"
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
      "$runtime_lib" -o "$tmp/rv64i-fullarith.elf"
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
          "$runtime_lib" -o "$tmp/$profile.elf"
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
    echo 'RV64_DIVREM_QEMU=PASS cases=14 profiles=3 signed_unsigned_32_64=1 gcc_oracle=1 upstream_libgcc_archive=1'

    # Checked overflow cannot depend on M instructions, may call two helpers,
    # and must preserve the wrapped result, pointer and original return address.
    cat >"$tmp/checkedmul.i" <<'SRC'
static int signed_checked(long a, long b, long wrapped, int overflow) {
  long out = 123;
  int flag = __builtin_mul_overflow(a, b, &out);
  return flag == overflow && out == wrapped;
}
static int unsigned_checked(unsigned long a, unsigned long b,
                            unsigned long wrapped, int overflow) {
  unsigned long out = 123;
  int flag = __builtin_mul_overflow(a, b, &out);
  return flag == overflow && out == wrapped;
}
static int narrow_checked(int a, int b, int wrapped, int overflow) {
  int out = 123;
  int flag = __builtin_mul_overflow(a, b, &out);
  return flag == overflow && out == wrapped;
}
static int narrow_unsigned_checked(unsigned a, unsigned b,
                                   unsigned wrapped, int overflow) {
  unsigned out = 123;
  int flag = __builtin_mul_overflow(a, b, &out);
  return flag == overflow && out == wrapped;
}
int evaluate(void) {
  long lo = (-9223372036854775807L - 1L);
  long hi = 9223372036854775807L;
  unsigned long umax = 18446744073709551615UL;
  if (!signed_checked(hi, 2L, -2L, 1)) return 1;
  if (!signed_checked(lo, -1L, lo, 1)) return 2;
  if (!signed_checked(-1L, lo, lo, 1)) return 3;
  if (!signed_checked(-7L, 8L, -56L, 0)) return 4;
  if (!signed_checked(0L, lo, 0L, 0)) return 5;
  if (!unsigned_checked(umax, 2UL, umax - 1UL, 1)) return 6;
  if (!unsigned_checked(umax, 1UL, umax, 0)) return 7;
  if (!unsigned_checked(0UL, umax, 0UL, 0)) return 8;
  if (!narrow_checked(50000, 50000, -1794967296, 1)) return 9;
  if (!narrow_checked(-17, 3, -51, 0)) return 10;
  if (!narrow_unsigned_checked(4294967295U, 2U, 4294967294U, 1)) return 11;
  if (!narrow_unsigned_checked(7U, 9U, 63U, 0)) return 12;
  return 0;
}
SRC
    "$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
      -S "$tmp/checkedmul.i" -o "$tmp/checkedmul.s"
    grep -q 'call __muldi3' "$tmp/checkedmul.s"
    grep -q 'call __divdi3' "$tmp/checkedmul.s"
    grep -q 'call __udivdi3' "$tmp/checkedmul.s"
    grep -Eq '[[:space:]]+sd ra,' "$tmp/checkedmul.s"
    grep -Eq '[[:space:]]+ld ra,' "$tmp/checkedmul.s"
    cat >"$tmp/checkedmul-start.s" <<'SRC'
.section .text
.globl _start
_start:
  call evaluate
  li a7, 93
  ecall
SRC
    riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 \
      -nostdlib -static -no-pie -Wl,-e,_start \
      "$tmp/checkedmul-start.s" "$tmp/checkedmul.s" \
      "$runtime_lib" -o "$tmp/checkedmul.elf"
    set +e
    qemu-riscv64 "$tmp/checkedmul.elf" >"$tmp/checkedmul.out" 2>"$tmp/checkedmul.err"
    checked_rc=$?
    set -e
    if (( checked_rc != 0 )); then
      echo "RV64_CHECKED_MUL_QEMU=FAIL case=$checked_rc" >&2
      cat "$tmp/checkedmul.err" >&2
      exit 1
    fi
    echo 'RV64_CHECKED_MUL_QEMU=PASS cases=12 signed_unsigned_32_64=1 overflow_min_neg1=1'
    # The final ELF includes our upstream libgcc archive. Validate the
    # linked byte encodings too, not merely our .s or .o files.
    python3 "$machine_audit" "$tmp/checkedmul.elf" \
      "$tmp/rv64i-fullarith.elf" "$tmp/minic-rv64i.elf"
    # End-to-end staged MiniC driver: ISA/ABI flags must reach the C
    # frontend and assembler, and a caller-supplied runtime archive must
    # be honored by MiniLD after its referencing objects.
    minic_driver="$(dirname "$MINIC_CC")/minic"
    if [[ -x "$minic_driver" ]]; then
      "$minic_driver" -S -march=rv64i -mabi=lp64 \
        "$tmp/mulonly.i" -o "$tmp/driver-mul.s"
      grep -q 'call __muldi3' "$tmp/driver-mul.s"
      if "$minic_driver" -S -march=rv64i -mabi=lp64d \
           "$tmp/mulonly.i" -o "$tmp/driver-wrong.s" \
           >"$tmp/driver-wrong.out" 2>"$tmp/driver-wrong.err"; then
        echo 'RV64_DRIVER=FAIL ISA/ABI mismatch accepted' >&2; exit 1
      fi
      grep -q 'incompatible-riscv-isa-abi' "$tmp/driver-wrong.err"
      cat >"$tmp/driver-mul.c" <<'SRC'
#if defined(__riscv_float_abi_double) || !defined(__riscv_float_abi_soft)
#error wrong_riscv_lp64_predefines
#endif
long evaluate(long a, long b) { return a * b; }
SRC
      "$minic_driver" -c -ffreestanding -nostdinc \
        -march=rv64i -mabi=lp64 \
        "$tmp/driver-mul.c" -o "$tmp/driver-mul.o"
      cat >"$tmp/driver-start.s" <<'SRC'
.section .text
.globl _start
_start:
  li a0, 9
  li a1, 4
  call evaluate
  li a7, 93
  ecall
SRC
      riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -c \
        "$tmp/driver-start.s" -o "$tmp/driver-start.o"
      "$minic_driver" -static -nostdlib -march=rv64i -mabi=lp64 \
        "--rtlib=$runtime_lib" "$tmp/driver-start.o" "$tmp/driver-mul.o" \
        -o "$tmp/driver-result.elf"
      set +e
      qemu-riscv64 "$tmp/driver-result.elf" >"$tmp/driver-result.out" \
        2>"$tmp/driver-result.err"
      rc=$?
      set -e
      if (( rc != 36 )); then
        echo "RV64_DRIVER=FAIL linked runtime rc=$rc expected=36" >&2
        cat "$tmp/driver-result.err" >&2
        exit 1
      fi
      echo 'RV64_DRIVER_RT=PASS preprocess=lp64 compile=1 assemble=1 minild_archive=1 qemu=36'
    fi

    echo 'RV64_ISA_QEMU=PASS profiles=rv64i,rv64im gnu_minic_exit_exact=1'
  fi
fi
echo 'RV64_ISA_PROFILES=PASS rv64i_lp64=1 rv64im_lp64=1 rv64imafd_lp64d=1 runtime_libcalls=5 pointer_scale=1 rejected_isa_abi=1 default_byte_exact=1'
