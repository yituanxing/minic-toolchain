#!/usr/bin/env bash
# Original arm64 GNU zero-operand kernel effects, privileged DAIF only GAS,
# unprivileged YIELD/CSDB only real GNU QEMU.
set -Eeuo pipefail
minic=$1;test -x "$minic"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat >"$d/original.i" <<'SRC'
void mini_mask(void){asm volatile("msr daifset, #3" : : : "memory");}
void mini_unmask(void){asm volatile("msr daifclr, #3" : : : "memory");}
void mini_yield(void){asm volatile("yield" : : : "memory");}
void mini_csdb(void){asm volatile("hint #20" : : : "memory");}
void mini_dmb(void){asm volatile("dmb ish" : : : "memory");}
SRC
sed 's/mini_/gnu_/g' "$d/original.i" >"$d/gnu.c"
"$minic" --target=aarch64-linux -S "$d/original.i" -o "$d/mini.s"
aarch64-linux-gnu-gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
aarch64-linux-gnu-gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
for op in 'msr daifset, #3' 'msr daifclr, #3' 'yield' 'hint #20' 'dmb ish';do
 grep -Fq "$op" "$d/mini.s"
done
cat >"$d/main.c" <<'SRC'
#include <stdio.h>
extern void mini_yield(void),gnu_yield(void);
extern void mini_csdb(void),gnu_csdb(void);
extern void mini_dmb(void),gnu_dmb(void);
int main(void){
 for(int i=0;i<1024;++i){
   mini_yield();gnu_yield();
   mini_csdb();gnu_csdb();
   mini_dmb();gnu_dmb();
 }
 puts("MINIC_AARCH64_ZERO_OPERAND_GNU_QEMU=PASS");
 return 42;
}
SRC
aarch64-linux-gnu-gcc -static -O0 "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run.elf"
result=0;qemu-aarch64 "$d/run.elf" || result=$?
test "$result" -eq 42
sed 's/msr daifset, #3/msr daifset, #5/' "$d/original.i" >"$d/bad-mask.i"
sed 's/msr daifclr, #3/msr daifclr, #3; brk #1/' "$d/original.i" >"$d/bad-inject.i"
# The original Linux __daif_local_irq_disable uses a plain volatile ASM
# without a memory clobber: this is valid and now tested separately.
# An unrelated hard-register clobber cannot be silently thrown away.
sed 's/"msr daifset, #3" : : : "memory"/"msr daifset, #3" : : : "x0"/' "$d/original.i" >"$d/bad-clobber.i"
sed 's/hint #20/hint #21/' "$d/original.i" >"$d/bad-hint.i"
for bad in bad-mask bad-inject bad-clobber bad-hint;do
 if "$minic" --target=aarch64-linux -S "$d/$bad.i" -o "$d/$bad.s" >/dev/null 2>&1;then
  echo "FAIL unexpected AArch privileged/hint ISA accepted $bad" >&2;exit 1
 fi
 test ! -e "$d/$bad.s"
done
echo 'MINIC_AARCH64_ZERO_OPERAND=PASS DAIF=2 HINT=2 DMB=1 GNU_QEMU=1 negatives=4'
