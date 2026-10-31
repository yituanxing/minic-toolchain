#!/usr/bin/env bash
# Original Linux LoongArch no-input DBAR hardware barriers, GNU GAS+QEMU.
set -Eeuo pipefail
mini=$1;test -x "$mini"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat >"$d/dbar.i" <<'SRC'
void mini_dbar0(void){asm volatile("dbar 0" : : : "memory");}
void mini_dbar700(void){asm volatile("dbar 0x700" : : : "memory");}
SRC
sed 's/mini_/gnu_/g' "$d/dbar.i" >"$d/gnu.c"
"$mini" --target=loongarch64-linux -S "$d/dbar.i" -o "$d/mini.s"
loongarch64-linux-gnu-gcc-13 -c -O0 "$d/gnu.c" -o "$d/gnu.o"
loongarch64-linux-gnu-gcc-13 -c -x assembler "$d/mini.s" -o "$d/mini.o"
grep -Fq 'dbar 0' "$d/mini.s"
grep -Fq 'dbar 0x700' "$d/mini.s"
cat >"$d/main.c" <<'SRC'
#include <stdio.h>
extern void mini_dbar0(void),gnu_dbar0(void);
extern void mini_dbar700(void),gnu_dbar700(void);
int main(void){
 for(int i=0;i<1024;++i){
  mini_dbar0();gnu_dbar0();mini_dbar700();gnu_dbar700();
 }
 puts("MINIC_LOONGARCH_ZERO_DBAR_GNU_QEMU=PASS");
 return 42;
}
SRC
loongarch64-linux-gnu-gcc-13 -static -O0 "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run.elf"
rc=0;qemu-loongarch64 "$d/run.elf" || rc=$?
test "$rc" -eq 42
sed 's/dbar 0x700/dbar 0x900/' "$d/dbar.i" >"$d/bad-immediate.i"
sed 's/dbar 0"/dbar 0; syscall 0"/' "$d/dbar.i" >"$d/bad-extra.i"
# Plain volatile DBAR is valid native ISA; a fabricated register clobber
# is not the same source contract and must still fail closed.
sed 's/"dbar 0" : : : "memory"/"dbar 0" : : : "a0"/' "$d/dbar.i" >"$d/bad-clobber.i"
for bad in bad-immediate bad-extra bad-clobber;do
 if "$mini" --target=loongarch64-linux -S "$d/$bad.i" -o "$d/$bad.s" >/dev/null 2>&1;then
  echo "FAIL LoongArch unsafe DBAR $bad accepted" >&2;exit 1
 fi
 test ! -e "$d/$bad.s"
done
echo 'MINIC_LOONGARCH_ZERO_DBAR=PASS ISA=2 GNU_QEMU=1 negatives=3'
