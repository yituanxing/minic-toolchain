#!/bin/sh
# Original C: PIC/PIE external libc data (optind) and function addresses
# cannot be ADR_PREL_PG_HI21; TU-local static data must stay correct.
# GNU owns preprocessing, GNU assembler, PIE/static link and QEMU oracle.
set -eu
: "$MINIC_CC"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
gcc=aarch64-linux-gnu-gcc
qemu=qemu-aarch64
cat >"$work/pic.c" <<'SRC'
#include <stdio.h>
#include <unistd.h>
static int local_value = 41;
int mini_pie_libc_address(void) {
    int *external = &optind;
    int (*external_function)(const char *) = &puts;
    (void)external_function("busybox-pie");
    return local_value + *external;
}
SRC
cat >"$work/main.c" <<'SRC'
extern int mini_pie_libc_address(void);
int main(void) { return mini_pie_libc_address() == 42 ? 0 : 3; }
SRC
"$gcc" -E -P -std=gnu11 "$work/pic.c" -o "$work/pic.i"
"$gcc" -c -O0 "$work/main.c" -o "$work/main.o"
"$gcc" -c -O0 -x c "$work/pic.i" -o "$work/pic.gnu.o"
# Exercise the actual driver admission path, not merely GNU's -pie linker.
"$MINIC_CC" --target=aarch64-linux -fPIE -S "$work/pic.i" -o "$work/pic.s"
grep -Fq 'adrp x9, :got:optind' "$work/pic.s"
grep -Fq 'adrp x9, :got:puts' "$work/pic.s"
grep -Fq 'adrp x9, local_value' "$work/pic.s"
"$gcc" -c -x assembler "$work/pic.s" -o "$work/pic.mini.o"
for linkage in pie static; do
    case "$linkage" in
        pie) link='-fPIE -pie' ;;
        static) link='-static' ;;
    esac
    "$gcc" $link "$work/pic.gnu.o" "$work/main.o" -o "$work/gnu"
    "$gcc" $link "$work/pic.mini.o" "$work/main.o" -o "$work/mini"
    timeout 40s "$qemu" -L /usr/aarch64-linux-gnu "$work/gnu" \
        >"$work/gnu.out" 2>"$work/gnu.err"
    timeout 40s "$qemu" -L /usr/aarch64-linux-gnu "$work/mini" \
        >"$work/mini.out" 2>"$work/mini.err"
    cmp "$work/gnu.out" "$work/mini.out"
    cmp "$work/gnu.err" "$work/mini.err"
    echo "AARCH64_PIE_EXTERN_GOT=PASS linkage=$linkage GNU_QEMU_byte_exact=1 libc_data=optind libc_function=puts TU_static=1"
done
