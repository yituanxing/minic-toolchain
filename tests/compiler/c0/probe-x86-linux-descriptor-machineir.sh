#!/bin/sh
# Restore Linux's exact 10-byte packed LGDT/LIDT and SGDT/SIDT contract.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
gnu=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/x86-descriptor-machineir
mkdir -p "$work"
cat >"$work/positive.c" <<'SOURCE'
struct desc_ptr { unsigned short size; unsigned long address; } __attribute__((packed));
void desc_load_idt(const struct desc_ptr *p) { asm volatile("lidt %0"::"m" (*p)); }
void desc_load_gdt(const struct desc_ptr *p) { asm volatile("lgdt %0"::"m" (*p)); }
void desc_store_gdt(struct desc_ptr *p) { asm volatile("sgdt %0":"=m" (*p)); }
void desc_store_idt(struct desc_ptr *p) { asm volatile("sidt %0":"=m" (*p)); }
SOURCE
"$gnu" -std=gnu11 -Werror -E -P -x c "$work/positive.c" -o "$work/positive.i"
"$gnu" -std=gnu11 -c "$work/positive.c" -o "$work/gnu.o"
"$minic" --target=x86_64-linux -S "$work/positive.i" -o "$work/minic.s"
"$gnu" -x assembler -c "$work/minic.s" -o "$work/minic.o"
for op in lidt lgdt sgdt sidt; do
    grep -Eq "^[[:space:]]+$op[[:space:]]+\\(%r10\\)" "$work/minic.s"
done
for bad in unpacked wrong-size inject; do
    case "$bad" in
        unpacked) sed 's/__attribute__((packed))//' "$work/positive.i" >"$work/bad.i" ;;
        wrong-size) sed 's/unsigned short size/unsigned int size/' "$work/positive.i" >"$work/bad.i" ;;
        inject) sed 's/lidt %0/lidt %0; int3/' "$work/positive.i" >"$work/bad.i" ;;
    esac
    rm -f "$work/bad.s"
    if "$minic" --target=x86_64-linux -S "$work/bad.i" -o "$work/bad.s" >"$work/bad.out" 2>"$work/bad.err"; then
        echo "X86_DESCRIPTOR_MACHINEIR=FAIL admitted=$bad" >&2; exit 1
    fi
    test ! -e "$work/bad.s"
done
printf '%s\n' 'X86_DESCRIPTOR_MACHINEIR=PASS forms=4 same_GNU_i=1 GNU_AS=1 negative=3'
