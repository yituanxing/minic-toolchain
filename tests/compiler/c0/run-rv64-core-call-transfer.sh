#!/usr/bin/env bash
# MiniC and GCC compile the same caller. GNU provides the callees and calls
# back into a MiniC function; the RISC-V ABI is checked in both directions.
set -Eeuo pipefail
: "${MINIC:?set MINIC}"
: "${BUILD_DIR:?set BUILD_DIR}"
RISCV_CC=${RISCV_CC:-riscv64-linux-gnu-gcc}
RISCV_LD=${RISCV_LD:-riscv64-linux-gnu-ld}
QEMU_RISCV64=${QEMU_RISCV64:-qemu-riscv64}
w="$BUILD_DIR/core-call-transfer"
rm -rf "$w"
mkdir -p "$w"
cat >"$w/client.c" <<'SRC'
/* 16 bytes, with a partially filled second integer ABI eightbyte. */
struct Pair { unsigned long tag; unsigned int count; unsigned short flags; };
extern long gnu_gp10(long,long,long,long,long,long,long,long,long,long);
extern long gnu_record(struct Pair,long);
extern double gnu_double(double,double);
extern long gnu_call_into_minic(void);
long minic_incoming_gp10(long a,long b,long c,long d,long e,
                         long f,long g,long h,long i,long j) {
    return a+b+c+d+e+f+g+h+i+j;
}
/* Mixed FPR/GPR/record named parameters: the callee reads a frozen plan. */
long minic_incoming_mixed(double a, struct Pair p, long tail) {
    return (long)a + (long)p.tag + (long)p.count + (long)p.flags + tail;
}
double minic_incoming_floats(double a, double b, double c) {
    return a + b + c;
}
extern long gnu_call_into_minic_mixed(void);
extern long gnu_call_into_minic_floats(void);
int rv64_transfer_entry(void) {
    struct Pair value;
    long (*gp)(long,long,long,long,long,long,long,long,long,long);
    long (*record)(struct Pair,long);
    double (*fp)(double,double);
    long direct, indirect, record_direct, record_indirect;
    double direct_fp, indirect_fp;
    value.tag = 13UL;
    value.count = 17U;
    value.flags = 5U;
    gp = gnu_gp10;
    record = gnu_record;
    fp = gnu_double;
    direct = gnu_gp10(1,2,3,4,5,6,7,8,9,10);
    indirect = gp(1,2,3,4,5,6,7,8,9,10);
    record_direct = gnu_record(value,7);
    record_indirect = record(value,7);
    direct_fp = gnu_double(1.25,2.25);
    indirect_fp = fp(1.25,2.25);
    return direct == 55 && indirect == 55 &&
           record_direct == 42 && record_indirect == 42 &&
           direct_fp == 3.5 && indirect_fp == 3.5 &&
           gnu_call_into_minic() == 55 &&
           gnu_call_into_minic_mixed() == 44 &&
           gnu_call_into_minic_floats() == 1 ? 42 : 1;
}
SRC
cat >"$w/provider.c" <<'SRC'
/* 16 bytes, with a partially filled second integer ABI eightbyte. */
struct Pair { unsigned long tag; unsigned int count; unsigned short flags; };
long gnu_gp10(long a,long b,long c,long d,long e,
              long f,long g,long h,long i,long j) {
    return a+b+c+d+e+f+g+h+i+j;
}
long gnu_record(struct Pair p,long extra) {
    return (long)p.tag + (long)p.count + (long)p.flags + extra;
}
double gnu_double(double a,double b) { return a+b; }
extern long minic_incoming_gp10(long,long,long,long,long,long,long,long,long,long);
long gnu_call_into_minic(void) {
    return minic_incoming_gp10(1,2,3,4,5,6,7,8,9,10);
}
extern long minic_incoming_mixed(double, struct Pair, long);
extern double minic_incoming_floats(double, double, double);
long gnu_call_into_minic_mixed(void) {
    struct Pair p;
    p.tag = 13UL;
    p.count = 17U;
    p.flags = 5U;
    return minic_incoming_mixed(2.0, p, 7);
}
long gnu_call_into_minic_floats(void) {
    return minic_incoming_floats(1.25, 2.25, 3.5) == 7.0;
}
SRC
cat >"$w/start.s" <<'ASM'
    .section .text.start,"ax",@progbits
    .globl _start
_start:
    call rv64_transfer_entry
    li a7, 93
    ecall
ASM
"$RISCV_CC" -fno-pie -fno-pic -O0 -c "$w/provider.c" -o "$w/provider.o"
"$RISCV_CC" -fno-pie -fno-pic -c "$w/start.s" -o "$w/start.o"
"$RISCV_CC" -fno-pie -fno-pic -O0 -c "$w/client.c" -o "$w/gnu-client.o"
# MiniC -S consumes preprocessed C; feed both compilers the same semantics.
"$RISCV_CC" -E -P -x c "$w/client.c" -o "$w/client.i"
"$MINIC" -S "$w/client.i" -o "$w/mini-client.s"
"$RISCV_CC" -fno-pie -fno-pic -c "$w/mini-client.s" -o "$w/mini-client.o"
for owner in gnu mini; do
    "$RISCV_LD" -static -e _start -o "$w/$owner.elf" \
        "$w/start.o" "$w/$owner-client.o" "$w/provider.o"
    set +e
    "$QEMU_RISCV64" "$w/$owner.elf" >"$w/$owner.out" 2>"$w/$owner.err"
    status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "RV64_CORE_CALL_TRANSFER=FAIL owner=$owner status=$status" >&2
        sed -n '1,12p' "$w/$owner.err" >&2
        exit 1
    fi
done
cmp "$w/gnu.out" "$w/mini.out"
cmp "$w/gnu.err" "$w/mini.err"
echo "RV64_CORE_CALL_TRANSFER=PASS direct=1 indirect=1 gp10_stack=1 record2=1 fp64=1 incoming_gnu=1 incoming_mixed_record=1 incoming_fp3=1 GNU_QEMU_diff=1"
