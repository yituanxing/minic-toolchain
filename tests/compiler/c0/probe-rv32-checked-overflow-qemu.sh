#!/usr/bin/env bash
# Strict RV32I checked-overflow GNU ELF32 + QEMU reference/MiniC differential.
# Runtime uses the existing pinned original GNU libgcc / LLVM compiler-rt.
set -Eeuo pipefail
: "$MINIC_CC" "$MINIAS"
test -x "$MINIC_CC"
test -x "$MINIAS"
root=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/edge.c" <<'SRC'
typedef signed short I16;
typedef signed int I32;
typedef unsigned int U32;
typedef long long I64;
typedef unsigned long long U64;
struct __attribute__((packed)) Guarded {
    unsigned char lead;
    I16 narrow;
    I32 word;
    U64 wide;
    unsigned char tail;
};
int s64_mul(I64 a, I64 b, I64 *r) { return __builtin_mul_overflow(a,b,r); }
int u64_mul(U64 a, U64 b, U64 *r) { return __builtin_mul_overflow(a,b,r); }
int u64_add(U64 a, U64 b, U64 *r) { return __builtin_add_overflow(a,b,r); }
int u64_sub(U64 a, U64 b, U64 *r) { return __builtin_sub_overflow(a,b,r); }
int mixed_mul(I32 a, U32 b, I32 *r) { return __builtin_mul_overflow(a,b,r); }
int mixed_wide(U64 a,U32 b,U32 *r) { return __builtin_add_overflow(a,b,r); }
int packed_add(I16 a,I16 b,struct Guarded *p) {
    return __builtin_add_overflow(a,b,&p->narrow);
}
int packed_wide(U64 a,U64 b,struct Guarded *p) {
    return __builtin_mul_overflow(a,b,&p->wide);
}
int main(void) {
    struct Guarded p;
    I64 sr;
    U64 wr;
    I32 sw;
    U32 nw;
    const I64 min = -9223372036854775807LL - 1LL;
    p.lead=0xa5U; p.tail=0x5aU;
    if (!s64_mul(min,-1LL,&sr) || sr!=min) return 11;
    if (s64_mul(min,1LL,&sr) || sr!=min) return 12;
    if (s64_mul(0LL,-1LL,&sr) || sr!=0LL) return 13;
    if (!u64_mul(0x100000000ULL,0x100000000ULL,&wr) || wr!=0ULL) return 14;
    if (u64_mul(0ULL,0xffffffffffffffffULL,&wr) || wr!=0ULL) return 15;
    if (!u64_add(0xffffffffffffffffULL,1ULL,&wr) || wr!=0ULL) return 16;
    if (!u64_sub(0ULL,1ULL,&wr) || wr!=0xffffffffffffffffULL) return 17;
    if (!mixed_mul(-1000000000,3U,&sw) || (U32)sw!=1294967296U) return 18;
    if (!mixed_wide(0x100000000ULL,7U,&nw) || nw!=7U) return 19;
    if (!packed_add((I16)32767,(I16)1,&p) || p.narrow!=(I16)-32768) return 20;
    if (!packed_wide(0x100000000ULL,0x100000000ULL,&p) || p.wide!=0ULL) return 21;
    if (p.lead!=0xa5U || p.tail!=0x5aU) return 22;
    return 0;
}
SRC

cat >"$work/start.s" <<'SRC'
.text
.globl _start
_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    call main
    li a7, 93
    ecall
SRC
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -c "$work/start.s" -o "$work/start.o"
bash "$root/tests/compiler/c0/build-rv32-gcc-runtime.sh" "$work"
mapfile -t runtime < "$work/rv32-runtime-objects.rsp"
test -s "$work/rv32-runtime-objects.rsp"
for fixture in packed edge; do
    case "$fixture" in
        packed) input="$root/tests/compiler/c0/riscv_packed_overflow_alignment.c" ;;
        edge) input="$work/edge.c" ;;
    esac
    riscv64-linux-gnu-gcc -E -P -x c "$input" -o "$work/$fixture.i"
    "$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 -S "$work/$fixture.i" -o "$work/$fixture.s"
    riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -c -x assembler "$work/$fixture.s" -o "$work/$fixture-minic-gas.o"
    "$MINIAS" -march=rv32i -mabi=ilp32 -o "$work/$fixture-minic-as.o" "$work/$fixture.s"
    riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -std=gnu11 -O1 -fno-inline -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -c "$work/$fixture.i" -o "$work/$fixture-gnu.o"
    for producer in gnu minic-gas minic-as; do
        elf="$work/$fixture-$producer.elf"
        riscv64-linux-gnu-ld -m elf32lriscv -static -e _start -o "$elf" "$work/start.o" "$work/$fixture-$producer.o" "${runtime[@]}"
        riscv64-linux-gnu-readelf -h "$elf" | grep -Eq 'Class:[[:space:]]+ELF32'
        riscv64-linux-gnu-readelf -h "$elf" | grep -Eq 'Machine:[[:space:]]+RISC-V'
        rc=0
        timeout 30s qemu-riscv32 "$elf" || rc=$?
        if test "$rc" -ne 0; then
            echo "RV32_CHECKED_OVERFLOW_QEMU=FAIL fixture=$fixture producer=$producer exit=$rc" >&2
            exit 1
        fi
        echo "RV32_CHECKED_OVERFLOW_CASE=PASS fixture=$fixture producer=$producer elf=ELF32 march=rv32i mabi=ilp32"
    done
done
echo 'RV32_CHECKED_OVERFLOW_QEMU=PASS GNU_C MiniC_GNU_AS MiniC_MiniAS 2fixtures'
