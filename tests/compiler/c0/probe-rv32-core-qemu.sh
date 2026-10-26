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
unsigned rv32_bitwise(unsigned a, unsigned b) { return (a & b) | (a ^ b); }
int rv32_shr_signed(int a, int s) { return a >> s; }
unsigned rv32_shr_unsigned(unsigned a, unsigned s) { return a >> s; }
int rv32_cmp_signed(int a, int b) { return a < b; }
int rv32_cmp_unsigned(unsigned a, unsigned b) { return a < b; }
unsigned rv32_shift_left(unsigned a, unsigned b) { return a << b; }
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
extern int gcc_vargs_wide(int first, ...);
extern int gcc_vargs_edge(int first, ...);
extern int gcc_vargs_double(int first, ...);
int call_varargs_11(void) {
    return gcc_vargs(1,2,3,4,5,6,7,8,9,10,11);
}
int call_varargs_wide(void) {
    return gcc_vargs_wide(3, 0x1122334455667788LL, 9, 10);
}
int call_varargs_edge(void) {
    return gcc_vargs_edge(1,2,3,4,5,6,7, 0x1122334455667788LL, 9);
}
int call_varargs_double(void) {
    return gcc_vargs_double(5, 6.5, 9);
}
struct CopyRecord { unsigned int x; unsigned char bytes[12]; int *pointer; };
static int copy_payload = 63;
int record_copy_probe(void) {
    struct CopyRecord in = { 4, {1,2,3,4,5,6,7,8,9,10,11,12},
                             &copy_payload };
    struct CopyRecord out;
    out = in;
    return (int)out.x + (int)out.bytes[11] + *out.pointer;
}
int record_load_probe(void) {
    struct CopyRecord in = { 7, {0}, &copy_payload };
    struct CopyRecord out = in;
    return (int)out.x + *out.pointer;
}
int call_callback(int n) { return callback.apply(n) + 7; }
struct Callback12 {
    int (*apply)(int,int,int,int,int,int,int,int,int,int,int,int);
};
static struct Callback12 indirect12 = { sum12 };
int call_indirect12(void) {
    return indirect12.apply(1,2,3,4,5,6,7,8,9,10,11,12);
}
long long wide_copy(long long value) { return value; }
long long wide_chain(long long value) { return wide_copy(value); }
double double_copy(double value) { return value; }
double double_chain(double value) { return double_copy(value); }
long long wide_signed(int value) { return (long long)value; }
long long wide_load(const long long *ptr) { return *ptr; }
void wide_store(long long *ptr, long long value) { *ptr = value; }
double df_add(double a, double b) { return a + b; }
double df_sub(double a, double b) { return a - b; }
double df_mul(double a, double b) { return a * b; }
double df_div(double a, double b) { return a / b; }
double df_neg(double a) { return -a; }
int df_eq(double a, double b) { return a == b; }
int df_lt(double a, double b) { return a < b; }
int df_le(double a, double b) { return a <= b; }
double df_from_int(int a) { return (double)a; }
int df_to_int(double a) { return (int)a; }
unsigned long long shift_wide_count(unsigned long long a,
                                    unsigned long long count) {
    return a >> count;
}
struct Rv32LargeStride { unsigned char payload[4096]; };
static struct Rv32LargeStride large_strides[3];
int rv32_stride_pow2(unsigned index) {
    large_strides[index].payload[17] = (unsigned char)(40U + index);
    return (int)large_strides[index].payload[17];
}
struct Rv32OddStride { unsigned char payload[13]; };
static struct Rv32OddStride odd_strides[3];
int rv32_stride_odd(unsigned index) {
    odd_strides[index].payload[5] = (unsigned char)(20U + index);
    return (int)odd_strides[index].payload[5];
}
float sf_add(float a, float b) { return a + b; }
float sf_sub(float a, float b) { return a - b; }
float sf_mul(float a, float b) { return a * b; }
float sf_div(float a, float b) { return a / b; }
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
    li a0, 10
    li a1, 6
    call rv32_bitwise
    li t0, 14
    bne a0, t0, fail
    li a0, -8
    li a1, 2
    call rv32_shr_signed
    li t0, -2
    bne a0, t0, fail
    li a0, 0x80000000
    li a1, 31
    call rv32_shr_unsigned
    li t0, 1
    bne a0, t0, fail
    li a0, -1
    li a1, 1
    call rv32_cmp_signed
    li t0, 1
    bne a0, t0, fail
    li a0, -1
    li a1, 1
    call rv32_cmp_unsigned
    bnez a0, fail
    li a0, 3
    li a1, 4
    call rv32_shift_left
    li t0, 48
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
    call call_varargs_wide
    li t0, 42
    bne a0, t0, fail
    call call_varargs_edge
    li t0, 43
    bne a0, t0, fail
    call call_varargs_double
    li t0, 44
    bne a0, t0, fail
    call record_copy_probe
    li t0, 79
    bne a0, t0, fail
    call record_load_probe
    li t0, 70
    bne a0, t0, fail
    li a0, 5
    call call_callback
    li t0, 17
    bne a0, t0, fail
    call call_indirect12
    li t0, 78
    bne a0, t0, fail
    li a0, 0x89abcdef
    li a1, 0x01234567
    call wide_chain
    li t0, 0x89abcdef
    bne a0, t0, fail
    li t0, 0x01234567
    bne a1, t0, fail
    li a0, 0x76543210
    li a1, 0xfedcba98
    call double_chain
    li t0, 0x76543210
    bne a0, t0, fail
    li t0, 0xfedcba98
    bne a1, t0, fail
    li a0, -17
    call wide_signed
    li t0, -17
    bne a0, t0, fail
    li t0, -1
    bne a1, t0, fail
    la a0, wide_memory
    call wide_load
    li t0, 0x12345678
    bne a0, t0, fail
    li t0, 0x9abcdef0
    bne a1, t0, fail
    la a0, wide_memory
    li a1, 0x34567890
    li a2, 0xabcdef12
    call wide_store
    la t1, wide_memory
    lw t2, 0(t1)
    li t0, 0x34567890
    bne t2, t0, fail
    lw t2, 4(t1)
    li t0, 0xabcdef12
    bne t2, t0, fail
    li a0, 0
    li a1, 0x40180000
    li a2, 0
    li a3, 0x40000000
    call df_add
    bnez a0, fail
    li t0, 0x40200000
    bne a1, t0, fail
    li a0, 0
    li a1, 0x40180000
    li a2, 0
    li a3, 0x40000000
    call df_sub
    bnez a0, fail
    li t0, 0x40100000
    bne a1, t0, fail
    li a0, 0
    li a1, 0x40180000
    li a2, 0
    li a3, 0x40000000
    call df_mul
    bnez a0, fail
    li t0, 0x40280000
    bne a1, t0, fail
    li a0, 0
    li a1, 0x40180000
    li a2, 0
    li a3, 0x40000000
    call df_div
    bnez a0, fail
    li t0, 0x40080000
    bne a1, t0, fail
    li a0, 0
    li a1, 0x40180000
    call df_neg
    bnez a0, fail
    li t0, 0xc0180000
    bne a1, t0, fail
    li a0, 0
    li a1, 0x40180000
    li a2, 0
    li a3, 0x40180000
    call df_eq
    li t0, 1
    bne a0, t0, fail
    li a0, 0
    li a1, 0x40000000
    li a2, 0
    li a3, 0x40180000
    call df_lt
    li t0, 1
    bne a0, t0, fail
    li a0, 0
    li a1, 0x40180000
    li a2, 0
    li a3, 0x40000000
    call df_le
    bnez a0, fail
    li a0, -9
    call df_from_int
    bnez a0, fail
    li t0, 0xc0220000
    bne a1, t0, fail
    li a0, 0
    li a1, 0x40180000
    call df_to_int
    li t0, 6
    bne a0, t0, fail
    li a0, 0
    li a1, 0x80000000
    li a2, 33
    li a3, 0
    call shift_wide_count
    li t0, 0x40000000
    bne a0, t0, fail
    bnez a1, fail
    # XLEN-local shift, 4096-byte stride: index 2 -> 42.
    li a0, 2
    call rv32_stride_pow2
    li t0, 42
    bne a0, t0, fail
    # Software libgcc multiplication, 13-byte stride: index 2 -> 22.
    li a0, 2
    call rv32_stride_odd
    li t0, 22
    bne a0, t0, fail
    # Single-precision binary32 / soft-float integer register ABI.
    li a0, 0x3fc00000  # 1.5f
    li a1, 0x40200000  # 2.5f
    call sf_add
    li t0, 0x40800000  # 4.0f
    bne a0, t0, fail
    li a0, 0x3fc00000
    li a1, 0x40200000
    call sf_sub
    li t0, 0xbf800000  # -1.0f
    bne a0, t0, fail
    li a0, 0x3fc00000
    li a1, 0x40200000
    call sf_mul
    li t0, 0x40700000  # 3.75f
    bne a0, t0, fail
    li a0, 0x40c00000  # 6.0f
    li a1, 0x40000000  # 2.0f
    call sf_div
    li t0, 0x40400000  # 3.0f
    bne a0, t0, fail
    # Preserve a signed -0 from -0 / +2.
    li a0, 0x80000000
    li a1, 0x40000000
    call sf_div
    li t0, 0x80000000
    bne a0, t0, fail
    # Zero over zero must be a NaN, not finite or infinity.
    li a0, 0
    li a1, 0
    call sf_div
    li t0, 0x7f800000
    and t1, a0, t0
    bne t1, t0, fail
    not t0, t0
    and t1, a0, t0
    beqz t1, fail
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
    li a7, 93
    ecall
.data
.balign 8
wide_memory:
    .word 0x12345678,0x9abcdef0
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
int gcc_vargs_wide(int first, ...) {
    __builtin_va_list cursor;
    long long value;
    int a, b;
    __builtin_va_start(cursor, first);
    value = __builtin_va_arg(cursor, long long);
    a = __builtin_va_arg(cursor, int);
    b = __builtin_va_arg(cursor, int);
    __builtin_va_end(cursor);
    return first == 3 && value == 0x1122334455667788LL &&
           a == 9 && b == 10 ? 42 : 2;
}
int gcc_vargs_edge(int first, ...) {
    __builtin_va_list cursor;
    int i, valid = first == 1;
    long long value;
    __builtin_va_start(cursor, first);
    for (i = 2; i <= 7; ++i)
        if (__builtin_va_arg(cursor, int) != i) valid = 0;
    value = __builtin_va_arg(cursor, long long);
    if (value != 0x1122334455667788LL) valid = 0;
    if (__builtin_va_arg(cursor, int) != 9) valid = 0;
    __builtin_va_end(cursor);
    return valid ? 43 : 3;
}
int gcc_vargs_double(int first, ...) {
    __builtin_va_list cursor;
    double value;
    int tail;
    __builtin_va_start(cursor, first);
    value = __builtin_va_arg(cursor, double);
    tail = __builtin_va_arg(cursor, int);
    __builtin_va_end(cursor);
    return first == 5 && value == 6.5 && tail == 9 ? 44 : 4;
}
SRC
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -ffreestanding -O0 \
    -c "$work/varargs.c" -o "$work/varargs.o"
bash tests/compiler/c0/build-rv32-gcc-runtime.sh "$work"
# This runtime manifest is the single link closure for every independent
# GNU/MiniAS/GNU-C ELF32 oracle. Listing only the historical two GNU
# integer objects silently excluded the new compiler-rt binary64 helpers.
mapfile -t rv32_runtime_objects < "$work/rv32-runtime-objects.rsp"
test "${#rv32_runtime_objects[@]}" -ge 17
for runtime_object in "${rv32_runtime_objects[@]}"; do
    test -s "$runtime_object"
done
riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/prog.elf" "$work/start.o" "$work/core.o" \
    "$work/varargs.o" "${rv32_runtime_objects[@]}"
# Exact GCC model is an independent oracle for the new ELF32 record+symbol
# relocations. Keep GNU's linker/syscall startup/library identical.
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -ffreestanding \
    -x c -c "$work/core.i" -o "$work/core-gcc.o"
riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/prog-gcc.elf" "$work/start.o" "$work/core-gcc.o" \
    "$work/varargs.o" "${rv32_runtime_objects[@]}"
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
    "$work/varargs.o" "${rv32_runtime_objects[@]}"
qemu-riscv32 "$work/prog-native.elf"
echo 'RV32_NATIVE_CHAIN=PASS minic_C=true minic_as_ELF32=true GNU_LD=true qemu_riscv32=true'
riscv64-linux-gnu-readelf -h "$work/prog.elf" |
  grep -q 'Class:.*ELF32'
# The 64-bit shift count checks both XLENs. The new 4096-byte
# pointer multiplication must use a Base-I shift, not M or a libcall.
grep -q 'bnez t5, .Lshift_wide_count_wide_ge64_' "$work/core.s"
grep -Fq 'slli t1, t1, 12' "$work/core.s"
grep -Fq 'call __mulsi3' "$work/core.s"
for symbol in __addsf3 __subsf3 __mulsf3 __divsf3; do
    grep -Fq "call $symbol" "$work/core.s" || {
        echo "RV32_SF32=FAIL no_machine_call=$symbol" >&2; exit 1;
    }
done
echo 'RV32_CORE=PASS GNU_AS=PASS MiniAS=PASS QEMU=PASS ILP32=1 ELF32=1 pointer_pow2_shift=1 pointer_odd_libgcc=1 softfloat_binary32=4 ieee_signed_zero_nan=1'
