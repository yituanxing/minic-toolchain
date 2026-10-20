#!/bin/sh
# DataLayout must emit C static zero-initialized arrays of records,
# including TinyCC's 257 x five-field __vstack shape, at full size.
set -eu
: "$MINIC_CC"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/globals.c" <<'SRC'
struct StackValue {
    long long low, high;
    void *sym;
    int kind;
    unsigned char flag;
};
struct StackValue mini_record_stack[257];
const struct StackValue mini_const_zero_stack[257];
SRC
cat > "$work/driver.c" <<'SRC'
#include <stdio.h>
struct StackValue {
    long long low, high;
    void *sym;
    int kind;
    unsigned char flag;
};
extern struct StackValue mini_record_stack[257];
extern const struct StackValue mini_const_zero_stack[257];
int main(void) {
    unsigned long long total = 0;
    unsigned char *a = (unsigned char *)mini_record_stack;
    const unsigned char *b = (const unsigned char *)mini_const_zero_stack;
    unsigned long i;
    for (i=0; i<sizeof(mini_record_stack); ++i) total += a[i];
    for (i=0; i<sizeof(mini_const_zero_stack); ++i) total += b[i];
    mini_record_stack[256].kind = 41;
    mini_record_stack[256].low = 1;
    printf("sz=%lu zero=%llu last=%lld/%d\n",
           (unsigned long)sizeof(mini_record_stack),
           total, mini_record_stack[256].low,
           mini_record_stack[256].kind);
    return total == 0 && mini_record_stack[256].low == 1 &&
           mini_record_stack[256].kind == 41 ? 0 : 3;
}
SRC
for target in aarch64 loongarch64; do
    case "$target" in
      aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
      loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    "$gcc" -std=gnu11 -E -P "$work/globals.c" -o "$work/$target.i"
    "$gcc" -O0 -c "$work/driver.c" -o "$work/$target.driver.o"
    "$gcc" -O0 -c -x c "$work/$target.i" -o "$work/$target.gnu.o"
    "$gcc" -static "$work/$target.gnu.o" "$work/$target.driver.o" \
        -o "$work/$target.gnu"
    timeout 45s "$qemu" "$work/$target.gnu" \
        >"$work/$target.gnu.out" 2>"$work/$target.gnu.err"
    "$MINIC_CC" --target="$target-linux" -S "$work/$target.i" \
        -o "$work/$target.s"
    "$gcc" -c -x assembler "$work/$target.s" -o "$work/$target.mini.o"
    "$gcc" -static "$work/$target.mini.o" "$work/$target.driver.o" \
        -o "$work/$target.mini"
    timeout 45s "$qemu" "$work/$target.mini" \
        >"$work/$target.mini.out" 2>"$work/$target.mini.err"
    cmp "$work/$target.gnu.out" "$work/$target.mini.out"
    cmp "$work/$target.gnu.err" "$work/$target.mini.err"
    echo "FOUR_BACKEND_ZERO_RECORD_ARRAY=PASS target=$target entries=257 fields=5 GNU_QEMU_byte_exact=1 const_rodata=1"
done
