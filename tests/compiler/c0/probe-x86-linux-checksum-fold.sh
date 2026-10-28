#!/usr/bin/env bash
set -Eeuo pipefail
cc=$1;d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat >"$d/fold.i" <<'SRC'
typedef unsigned int u32;
u32 mini_fold(u32 x){
 u32 sum=x;
 asm("  addl %1,%0\n" "  adcl $0xffff,%0"
     : "=r"(sum) : "r"(x << 16), "0"(x & 0xffff0000U));
 return ~sum >> 16;
}
SRC
sed 's/mini_fold/gnu_fold/g' "$d/fold.i" >"$d/gnu.c"
"$cc" --target=x86_64-linux -S "$d/fold.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
grep -Fq 'addl %r10d, %eax' "$d/mini.s"
grep -Fq 'adcl $0xffff, %eax' "$d/mini.s"
cat >"$d/main.c" <<'SRC'
#include <stdio.h>
extern unsigned int mini_fold(unsigned int),gnu_fold(unsigned int);
int main(void){
 for(unsigned int i=0;i<32768U;++i){
   const unsigned int v=i*17627U+(i<<17);
   if(mini_fold(v)!=gnu_fold(v))return 1;
 }
 puts("MINIC_X86_CHECKSUM_FOLD_GNU_NATIVE=PASS cases=32768");
 return 42;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/gnu.o" "$d/mini.o" -o "$d/run"
result=0;"$d/run" || result=$?
test "$result" -eq 42
sed 's/adcl $0xffff,%0/adcl $0xffff,%0; hlt/' "$d/fold.i" >"$d/extra.i"
sed 's/"0"/"r"/' "$d/fold.i" >"$d/tie.i"
sed 's/"=r"(sum)/"=a"(sum)/' "$d/fold.i" >"$d/out.i"
for case in extra tie out;do
 if "$cc" --target=x86_64-linux -S "$d/$case.i" -o "$d/$case.s" >/dev/null 2>&1;then
  echo "FAIL unsafe x86 checksum $case" >&2;exit 1
 fi
 test ! -e "$d/$case.s"
done
echo "MINIC_X86_LINUX_CHECKSUM_FOLD=PASS GNU_native_cases=32768 negative=3"
