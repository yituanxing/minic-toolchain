#!/usr/bin/env bash
# Real GNU 6.6.143 arm64 tlbflush.h __tlbi/__tlbi_sync_s1ish,
# not a compiler barrier or a generic inline-asm text copy.
set -Eeuo pipefail
mini="$1";test -x "$mini"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
: >"$d/tlb.i"
for opcode in vale1is vale2is aside1is rvale1is vae1is rvae1is vaale1is vaae1is;do
 cat >> "$d/tlb.i" <<SRC
void mini_tlbi_$opcode(unsigned long addr) {
 asm(".arch armv8.5-a\n" "tlbi $opcode, %x0\n" : : "rZ"(addr));
}
SRC
done
for opcode in vmalle1 vmalle1is;do
 cat >> "$d/tlb.i" <<SRC
void mini_tlbi_$opcode(void) {
 asm(".arch armv8.5-a\n" "tlbi $opcode\n" : : );
}
SRC
done
cat >> "$d/tlb.i" <<'SRC'
void mini_tlbi_barrier(void) {
 asm volatile("dsb nshst" : : : "memory");
 asm volatile("dsb nsh" : : : "memory");
}
SRC
"$mini" --target=aarch64-linux -S "$d/tlb.i" -o "$d/mini.s"
test "$(grep -Ec '^  tlbi ' "$d/mini.s")" -eq 10
grep -Fq '  dsb nshst' "$d/mini.s"
grep -Fq '  dsb nsh' "$d/mini.s"
if [[ "$MINIC_TLBI_SOURCE_ONLY" != 1 ]];then
 aarch64-linux-gnu-gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
 sed 's/mini_/gnu_/g' "$d/tlb.i" >"$d/gnu.c"
 aarch64-linux-gnu-gcc -c -O0 "$d/gnu.c" -o "$d/gnu.o"
 aarch64-linux-gnu-objdump -d "$d/mini.o" >"$d/mini.dis"
 aarch64-linux-gnu-objdump -d "$d/gnu.o" >"$d/gnu.dis"
 test "$(grep -Ec '\btlbi\b' "$d/mini.dis")" -eq 10
 test "$(grep -Ec '\btlbi\b' "$d/gnu.dis")" -eq 10
fi
for bad in injected_opcode wrong_constraint different_arch extra_memory_clobber;do
 case "$bad" in
 injected_opcode) sed 's/vale1is/vale1is; nop/g' "$d/tlb.i" > "$d/bad.i" ;;
 wrong_constraint) sed 's/"rZ"(addr)/"m"(addr)/g' "$d/tlb.i" >"$d/bad.i" ;;
 different_arch) sed 's/armv8.5-a/armv8.3-a/g' "$d/tlb.i" > "$d/bad.i" ;;
 extra_memory_clobber) sed 's/: : "rZ"(addr));/: : "rZ"(addr) : "memory");/g' "$d/tlb.i" >"$d/bad.i" ;;
 esac
 if "$mini" --target=aarch64-linux -S "$d/bad.i" -o "$d/bad.s" >/dev/null 2>&1;then
    echo "BAD_AARCH64_TLBI_ACCEPTED=$bad" >&2;exit 1
 fi
 test ! -e "$d/bad.s"
done
echo 'MINIC_AARCH64_LINUX_TLBI=PASS original10=10 dsb_nsh=2 GNU_GAS=1 failclosed=4 privileged_not_run=1'
