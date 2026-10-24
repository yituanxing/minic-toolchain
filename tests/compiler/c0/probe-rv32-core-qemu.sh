#!/usr/bin/env bash
# True C -> Core -> RV32I -> GNU AS ELF32 -> GNU LD -> QEMU Linux.
# The same MiniC source remains fail-closed on unsupported wider ABI.
set -Eeuo pipefail
: "${MINIC_CC:?}"
: "${MINIAS:=${MINIC_CC%/minic-cc}/minic-as}"
test -x "$MINIAS"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/core.i" <<'SRC'
int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }
int absdiff(int a, int b) {
    int x = a - b;
    if (x < 0) x = -x;
    return x;
}
int triangular(int n) {
    int total = 0;
    while (n > 0) {
        total = total + n;
        n = n - 1;
    }
    return total;
}
int caller(int a, int b) {
    return add(a,b) + mul(a,b) + absdiff(a,b);
}
static const unsigned char keybox[8] = { 0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5 };
static const unsigned short halves[4] = { 0x0102,0xabcd,0x3e4d,0x0000 };
static unsigned int counter = 17;
static unsigned char unwritten[9];
int table_probe(int n) {
    counter = counter + (unsigned int)n;
    return keybox[n] + halves[1] + (int)counter + unwritten[n];
}
int ptr_probe(int n) {
    unsigned char buf[8];
    buf[0] = 3;
    buf[1] = 7;
    return buf[n];
}
int multiply_again(int x) { return x * 13; }
int signed_div(int a, int b) { return a / b; }
int signed_mod(int a, int b) { return a % b; }
unsigned unsigned_div(unsigned a, unsigned b) { return a / b; }
unsigned unsigned_mod(unsigned a, unsigned b) { return a % b; }
int sum9(int a,int b,int c,int d,int e,int f,int g,int h,int i) {
    return a+b+c+d+e+f+g+h+i;
}
int sum12(int a,int b,int c,int d,int e,int f,int g,int h,
          int i,int j,int k,int l) {
    return a+b+c+d+e+f+g+h+i+j+k+l;
}
int call9(void) { return sum9(1,2,3,4,5,6,7,8,9); }
int call12(void) { return sum12(1,2,3,4,5,6,7,8,9,10,11,12); }
struct Record { unsigned char first; unsigned int *second; unsigned short third; };
static unsigned int record_payload = 39;
static struct Record rec = { 3, &record_payload, 5 };
int record_init_probe(void) {
    return rec.first + *rec.second + rec.third;
}
static int plus_five(int value) { return value + 5; }
struct Callback { int (*apply)(int); unsigned tag; };
static struct Callback callback = { plus_five, 41 };
int global_function_reloc_probe(void) {
    return callback.apply != 0 && callback.tag == 41;
}
extern int gcc_vargs(int first, ...);
int call_varargs_11(void) {
    return gcc_vargs(1,2,3,4,5,6,7,8,9,10,11);
}
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/core.i" -o "$work/core.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -c \
    "$work/core.s" -o "$work/core.o"
"$MINIAS" -march=rv32i -mabi=ilp32 \
    -o "$work/core-native.o" "$work/core.s"
riscv64-linux-gnu-readelf -h "$work/core-native.o" |
  grep -q 'Class:.*ELF32'
cat >"$work/start.s" <<'SRC'
.text
.globl _start
_start:
    # Standalone assembly without a libc crt1 MUST establish RISC-V gp.
    # GNU ld relaxes both GNU C and MiniC references to .sdata globals
    # into gp-relative loads/stores. With gp=0, both GCC and MiniC crash
    # before returning, as independently confirmed in QEMU. The norelax
    # bracket is the standard RISC-V crt0 pattern.
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    li a0, 6
    li a1, 3
    call caller
    li t0, 30
    bne a0, t0, fail
    li a0, 5
    call triangular
    li t0, 15
    bne a0, t0, fail
    li a0, 1
    call table_probe
    li t0, 44123
    bne a0, t0, fail
    li a0, 1
    call ptr_probe
    li t0, 7
    bne a0, t0, fail
    li a0, 3
    call multiply_again
    li t0, 39
    bne a0, t0, fail
    li a0, -18
    li a1, 5
    call signed_div
    li t0, -3
    bne a0, t0, fail
    li a0, -18
    li a1, 5
    call signed_mod
    li t0, -3
    bne a0, t0, fail
    li a0, 1234
    li a1, 7
    call unsigned_div
    li t0, 176
    bne a0, t0, fail
    li a0, 1234
    li a1, 7
    call unsigned_mod
    li t0, 2
    bne a0, t0, fail
    call call9
    li t0, 45
    bne a0, t0, fail
    call call12
    li t0, 78
    bne a0, t0, fail
    # Independent caller exercises native ILP32 12-register/stack contract.
    addi sp, sp, -16
    li t0, 9
    sw t0, 0(sp)
    li t0, 10
    sw t0, 4(sp)
    li t0, 11
    sw t0, 8(sp)
    li t0, 12
    sw t0, 12(sp)
    li a0, 1
    li a1, 2
    li a2, 3
    li a3, 4
    li a4, 5
    li a5, 6
    li a6, 7
    li a7, 8
    call sum12
    addi sp, sp, 16
    li t0, 78
    bne a0, t0, fail
    call record_init_probe
    li t0, 47
    bne a0, t0, fail
    call global_function_reloc_probe
    li t0, 1
    bne a0, t0, fail
    call call_varargs_11
    li t0, 66
    bne a0, t0, fail
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
    li a7, 93
    ecall
SRC
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -c "$work/start.s" -o "$work/start.o"
# This independent GNU C callee uses *real* RISC-V GCC va_start/va_arg
# to consume register+stack arguments from the MiniC ILP32 call site.
cat >"$work/varargs.c" <<'SRC'
int gcc_vargs(int first, ...) {
    __builtin_va_list cursor;
    int total = first;
    int i;
    __builtin_va_start(cursor, first);
    for (i = 0; i < 10; ++i)
        total += __builtin_va_arg(cursor, int);
    __builtin_va_end(cursor);
    return total;
}
SRC
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -ffreestanding -O0 \
    -c "$work/varargs.c" -o "$work/varargs.o"
bash tests/compiler/c0/build-rv32-gcc-runtime.sh "$work"
riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/prog.elf" "$work/start.o" "$work/core.o" \
    "$work/varargs.o" "$work/libgcc-mulsi3.o" "$work/libgcc-divsi3.o"
# Exact GCC model is an independent oracle for the new ELF32 record+symbol
# relocations. Keep GNU's linker/syscall startup/library identical.
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -ffreestanding \
    -x c -c "$work/core.i" -o "$work/core-gcc.o"
riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/prog-gcc.elf" "$work/start.o" "$work/core-gcc.o" \
    "$work/varargs.o" "$work/libgcc-mulsi3.o" "$work/libgcc-divsi3.o"
qemu-riscv32 "$work/prog-gcc.elf"
set +e
timeout 30s qemu-riscv32 -strace "$work/prog.elf" \
    >"$work/minic.stdout" 2>"$work/minic.stderr"
rv32_status=$?
set -e
if [[ "$rv32_status" != 0 ]]; then
    echo "RV32_CORE=FAIL stage=GNU_AS_QEMU exit=$rv32_status" >&2
    cat "$work/minic.stderr" >&2
    riscv64-linux-gnu-nm -n "$work/prog.elf" |
      grep -E 'rec|callback|record_payload|plus_five|start' >&2 || :
    riscv64-linux-gnu-objdump -s -j .data "$work/prog.elf" |
      tail -n 30 >&2 || :
    riscv64-linux-gnu-objdump -d "$work/prog.elf" |
      sed -n '/<record_init_probe>:/,/^$/p;/<global_function_reloc_probe>:/,/^$/p' >&2 || :
    exit "$rv32_status"
fi
# Second independent assembly producer for identical MiniC/Core C program.
# The GNU linker is retained as the oracle because generic MiniLD ELF32
# program linking is not yet qualified (only compat-vDSO is certified).
riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/prog-native.elf" "$work/start.o" "$work/core-native.o" \
    "$work/varargs.o" "$work/libgcc-mulsi3.o" "$work/libgcc-divsi3.o"
qemu-riscv32 "$work/prog-native.elf"
echo 'RV32_NATIVE_CHAIN=PASS minic_C=true minic_as_ELF32=true GNU_LD=true qemu_riscv32=true'
riscv64-linux-gnu-readelf -h "$work/prog.elf" |
  grep -q 'Class:.*ELF32'
# RV32 frontend must not claim multi-XLEN scalar arithmetic prematurely.
cat >"$work/wide.i" <<'SRC'
long long wide(long long x) { return x + 1; }
SRC
if "$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/wide.i" -o "$work/wide.s" 2>"$work/wide.err"; then
    echo 'RV32_CORE=FAIL unexpectedly accepted wide return ABI' >&2
    exit 1
fi
grep -Eq 'rv32i:|not yet qualified' "$work/wide.err"
echo 'RV32_CORE=PASS C_functions=20 ILP32=1 ELF32=1 QEMU=PASS GNU_AS=PASS MiniAS=PASS GNU_LD=PASS RV32I_libgcc_muldivmod=PASS variadic_integer_stack_ABI=PASS wide_ABI=reject'
