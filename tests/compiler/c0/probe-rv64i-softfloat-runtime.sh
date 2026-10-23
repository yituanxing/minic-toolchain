#!/usr/bin/env bash
# Executable psABI + IEEE-754 acceptance of the pinned upstream runtime.
# Usage: probe-rv64i-softfloat-runtime.sh MINIC_CC ARCHIVE MACHINE_AUDIT WORKDIR
set -Eeuo pipefail
minic_cc="$1"
runtime="$2"
machine_audit="$3"
work="$4"
mkdir -p "$work"
test -s "$runtime"
command -v riscv64-linux-gnu-gcc >/dev/null
command -v riscv64-linux-gnu-nm >/dev/null
command -v qemu-riscv64 >/dev/null

# Independent RV64I/LP64 GNU GCC caller, MiniC callee and compiler-rt provider.
# Every FP operation has a separate ABI boundary (no private within-TU ABI
# agreement can accidentally conceal passing a float in the wrong register).
cat >"$work/minic-fp.i" <<'SRC'
float sf_add(float x, float y) { return x + y; }
float sf_sub(float x, float y) { return x - y; }
float sf_mul(float x, float y) { return x * y; }
float sf_div(float x, float y) { return x / y; }
double df_add(double x, double y) { return x + y; }
double df_sub(double x, double y) { return x - y; }
double df_mul(double x, double y) { return x * y; }
double df_div(double x, double y) { return x / y; }
SRC
"$minic_cc" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$work/minic-fp.i" -o "$work/minic-fp.s"
for sym in __addsf3 __subsf3 __mulsf3 __divsf3 \
           __adddf3 __subdf3 __muldf3 __divdf3; do
  grep -q "call $sym" "$work/minic-fp.s" || {
    echo "RV64_SOFTFP=FAIL MiniC missing upstream helper $sym" >&2
    exit 1
  }
done
if grep -Eq '^[[:space:]]*(fadd|fsub|fmul|fdiv|fmv|fcvt|fld|fsd)\.' \
    "$work/minic-fp.s"; then
  echo 'RV64_SOFTFP=FAIL MiniC emitted hardware FP instructions' >&2
  exit 1
fi

# Expected bits are independent hand-checked IEEE-754 constants, not another
# compiler-rt call. GCC only constructs input payloads and checks integer bits.
cat >"$work/gnu-oracle.c" <<'SRC'
typedef union { float value; unsigned bits; } F;
typedef union { double value; unsigned long bits; } D;
static void show_failed_double(unsigned long bits) {
  register unsigned long a0 asm("a0") = 2UL;
  register unsigned long a1 asm("a1") = (unsigned long)&bits;
  register unsigned long a2 asm("a2") = 8UL;
  register unsigned long a7 asm("a7") = 64UL;
  asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}
extern float sf_add(float, float), sf_sub(float, float);
extern float sf_mul(float, float), sf_div(float, float);
extern double df_add(double, double), df_sub(double, double);
extern double df_mul(double, double), df_div(double, double);
int evaluate(void) {
  F fa = { .bits = 0x3fc00000U }, fb = { .bits = 0x40100000U };
  F f2 = { .bits = 0x40000000U }, f9 = { .bits = 0x41100000U };
  F f4 = { .bits = 0x40800000U }, f;
  D da = { .bits = 0x3ff8000000000000UL };
  D db = { .bits = 0x4002000000000000UL };
  D d2 = { .bits = 0x4000000000000000UL };
  D d9 = { .bits = 0x4022000000000000UL };
  D d4 = { .bits = 0x4010000000000000UL }, d;
  f.value = sf_add(fa.value, fb.value);
  if (f.bits != 0x40700000U) return 1; // 1.5 + 2.25 = 3.75
  f.value = sf_sub(fa.value, fb.value);
  if (f.bits != 0xbf400000U) return 2; // 1.5 - 2.25 = -0.75
  f.value = sf_mul(fa.value, f2.value);
  if (f.bits != 0x40400000U) return 3; // 1.5 * 2 = 3
  f.value = sf_div(f9.value, f4.value);
  if (f.bits != 0x40100000U) return 4; // 9 / 4 = 2.25
  d.value = df_add(da.value, db.value);
  if (d.bits != 0x400e000000000000UL) {
    show_failed_double(d.bits);
    return 5;
  }
  d.value = df_sub(da.value, db.value);
  if (d.bits != 0xbfe8000000000000UL) return 6;
  d.value = df_mul(da.value, d2.value);
  if (d.bits != 0x4008000000000000UL) return 7;
  d.value = df_div(d9.value, d4.value);
  if (d.bits != 0x4002000000000000UL) return 8;
  {
    F s = { .bits = 1U };
    f.value = sf_add(s.value, s.value); // subnormal preserved
    if (f.bits != 2U) return 9;
  }
  {
    D s = { .bits = 1UL };
    d.value = df_add(s.value, s.value);
    if (d.bits != 2UL) return 10;
  }
  {
    F one = { .bits = 0x3f800000U };
    F half_ulp = { .bits = 0x33800000U };
    f.value = sf_add(one.value, half_ulp.value); // ties to even
    if (f.bits != 0x3f800000U) return 11;
  }
  {
    D inf = { .bits = 0x7ff0000000000000UL };
    D ninf = { .bits = 0xfff0000000000000UL };
    d.value = df_add(inf.value, ninf.value);
    if ((d.bits & 0x7ff0000000000000UL) != 0x7ff0000000000000UL ||
        (d.bits & 0x000fffffffffffffUL) == 0UL) return 12;
  }
  {
    D max = { .bits = 0x7fefffffffffffffUL };
    d.value = df_mul(max.value, d2.value);
    if (d.bits != 0x7ff0000000000000UL) return 13;
  }
  {
    D zero = { .bits = 0UL };
    d.value = df_div(da.value, zero.value);
    if (d.bits != 0x7ff0000000000000UL) return 14;
  }
  {
    D pz = { .bits = 0UL }, nz = { .bits = 0x8000000000000000UL };
    d.value = df_add(nz.value, pz.value);
    if (d.bits != 0UL) return 15; // nearest-even: -0 + +0 => +0
  }
  return 0;
}
SRC
cat >"$work/start.s" <<'SRC'
.section .text
.globl _start
_start:
  call evaluate
  li a7, 93
  ecall
SRC
riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -O0 -ffreestanding \
  -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
  -c "$work/gnu-oracle.c" -o "$work/gnu-oracle.o"
# Isolate the runtime independently of MiniC ABI lowering. This
# directly passes IEEE-754 double payloads as a0/a1 and checks the raw a0.
cat >"$work/runtime-direct.s" <<'SRC'
.section .text
.globl _start
_start:
  li a0, 0x3ff8000000000000
  li a1, 0x4002000000000000
  call __adddf3
  li t0, 0x400e000000000000
  beq a0, t0, 1f
  addi sp, sp, -16
  sd a0, 0(sp)
  li a0, 2
  mv a1, sp
  li a2, 8
  li a7, 64
  ecall
  addi sp, sp, 16
  li a0, 1
  j 2f
1: li a0, 0
2: li a7, 93
  ecall
SRC
riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -nostdlib \
  -static -no-pie -Wl,-e,_start "$work/runtime-direct.s" "$runtime" \
  -o "$work/runtime-direct.elf"
set +e
qemu-riscv64 "$work/runtime-direct.elf" >"$work/runtime-direct.out" \
  2>"$work/runtime-direct.err"
direct_rc=$?
set -e
if (( direct_rc != 0 )); then
  echo "RV64_SOFTFP_RUNTIME_DIRECT=FAIL __adddf3 rc=$direct_rc" >&2
  cat "$work/runtime-direct.err" >&2
  if [[ -s "$work/runtime-direct.err" ]]; then
    echo "RV64_SOFTFP_RUNTIME_DIRECT=actual_bits:" >&2
    od -An -tx8 "$work/runtime-direct.err" >&2
  fi
  echo 'RV64_SOFTFP_DEBUG=caller:' >&2
  riscv64-linux-gnu-objdump -d -M no-aliases "$work/runtime-direct.elf" |
    sed -n '/<_start>:/,+17p' >&2
  echo 'RV64_SOFTFP_DEBUG=callee:' >&2
  riscv64-linux-gnu-objdump -d -M no-aliases "$work/runtime-direct.elf" |
    sed -n '/<__adddf3>:/,+90p' >&2
  echo 'RV64_SOFTFP_DEBUG=runtime_abi_flags:' >&2
  riscv64-linux-gnu-readelf -h "$(dirname "$runtime")/softfp/adddf3.o" |
    grep -E 'Machine:|Flags:' >&2
  exit 1
fi
echo 'RV64_SOFTFP_RUNTIME_DIRECT=PASS __adddf3 pure_integer_ABI=1'
riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 \
  -c "$work/minic-fp.s" -o "$work/minic-fp-gnu-as.o"
python3 "$machine_audit" "$work/gnu-oracle.o" "$work/minic-fp-gnu-as.o"
# A missing archive must fail at final link: no fake implementation or
# accidental default LP64D system libgcc is allowed.
if riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -nostdlib \
  -static -no-pie -Wl,-e,_start "$work/start.s" "$work/gnu-oracle.o" \
  "$work/minic-fp-gnu-as.o" -o "$work/no-runtime.elf" \
  >"$work/no-runtime.out" 2>"$work/no-runtime.err"; then
  echo 'RV64_SOFTFP=FAIL linked without explicit softfloat runtime' >&2
  exit 1
fi
grep -Eq 'undefined reference.*__(add|sub|mul|div)(sf|df)3' \
  "$work/no-runtime.err" || {
    cat "$work/no-runtime.err" >&2; exit 1;
  }
# First link via the GNU oracle assembler.
riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -nostdlib \
  -static -no-pie -Wl,-e,_start "$work/start.s" "$work/gnu-oracle.o" \
  "$work/minic-fp-gnu-as.o" "$runtime" -o "$work/softfp-gnu-as.elf"
set +e
qemu-riscv64 "$work/softfp-gnu-as.elf" >"$work/softfp-gnu-as.out" \
  2>"$work/softfp-gnu-as.err"
rc=$?
set -e
if (( rc != 0 )); then
  echo "RV64_SOFTFP_QEMU=FAIL source=GNU-AS case=$rc" >&2
  cat "$work/softfp-gnu-as.err" >&2
  if [[ -s "$work/softfp-gnu-as.err" ]]; then
    echo 'RV64_SOFTFP_QEMU=actual_64bit_hex:' >&2
    od -An -tx8 "$work/softfp-gnu-as.err" >&2
  fi
  exit 1
fi
python3 "$machine_audit" "$work/softfp-gnu-as.elf"

# Then rerun the exact same executable workload through our assembler,
# if it is present in the same staged toolchain.
minias="$(dirname "$minic_cc")/minic-as"
if [[ -x "$minias" ]]; then
  "$minias" -march=rv64i -mabi=lp64 \
    -o "$work/minic-fp-minias.o" "$work/minic-fp.s"
  python3 "$machine_audit" "$work/minic-fp-minias.o"
  riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -nostdlib \
    -static -no-pie -Wl,-e,_start "$work/start.s" "$work/gnu-oracle.o" \
    "$work/minic-fp-minias.o" "$runtime" -o "$work/softfp-minias.elf"
  set +e
  qemu-riscv64 "$work/softfp-minias.elf" >"$work/softfp-minias.out" \
    2>"$work/softfp-minias.err"
  rc=$?
  set -e
  if (( rc != 0 )); then
    echo "RV64_SOFTFP_QEMU=FAIL source=MiniAS case=$rc" >&2
    cat "$work/softfp-minias.err" >&2
    exit 1
  fi
  python3 "$machine_audit" "$work/softfp-minias.elf"
  echo 'RV64_SOFTFP_MINIAS=PASS ISA=rv64i psABI=lp64'

  # Verify *our* linker extracts the same LLVM compiler-rt members on demand
  # from the same archive as GNU ld, without substituting a default LP64D
  # toolchain or reassembling MiniC's code via GNU AS.
  minic_driver="$(dirname "$minic_cc")/minic"
  if [[ -x "$minic_driver" ]]; then
    riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 \
      -c "$work/start.s" -o "$work/start.o"
    "$minic_driver" -static -nostdlib -march=rv64i -mabi=lp64 \
      "--rtlib=$runtime" "$work/start.o" "$work/gnu-oracle.o" \
      "$work/minic-fp-minias.o" -o "$work/softfp-minild.elf"
    set +e
    qemu-riscv64 "$work/softfp-minild.elf" >"$work/softfp-minild.out" \
      2>"$work/softfp-minild.err"
    rc=$?
    set -e
    if (( rc != 0 )); then
      echo "RV64_SOFTFP_QEMU=FAIL source=MiniLD case=$rc" >&2
      cat "$work/softfp-minild.err" >&2
      exit 1
    fi
    python3 "$machine_audit" "$work/softfp-minild.elf"
    echo 'RV64_SOFTFP_MINILD=PASS minic_driver=1 minias=1 archive=17 gnu_caller=1 cases=15'
  fi
fi

# Runtime implementation objects themselves must not contain illegal
# opcodes even if archive extraction did not need every member.
for obj in "$(dirname "$runtime")"/softfp/*.o; do
  python3 "$machine_audit" "$obj"
done
echo 'RV64_SOFTFP_QEMU=PASS cases=15 gcc_caller=1 minic_callee=1 upstream_runtime=1 ieee754=1 base_i=1'
