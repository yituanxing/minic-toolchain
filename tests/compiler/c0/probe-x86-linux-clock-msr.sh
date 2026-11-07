#!/usr/bin/env bash
# x86-64 rdtsc/rdpmc/rdtscp: native hardware ISA and GNU GAS tests.
set -Eeuo pipefail
minic=$1;test -x "$minic"
w=$(mktemp -d);trap 'rm -rf "$w"' EXIT
cat >"$w/counter.i" <<'SRC'
void mini_tick32(unsigned int *low,unsigned int *high){
    asm volatile("rdtsc" : "=a"(*low), "=d"(*high));
}
void mini_tick64(unsigned long *low,unsigned long *high){
    asm volatile("rdtsc" : "=a"(*low), "=d"(*high));
}
void mini_pmc(unsigned int idx,unsigned int *low,unsigned int *high){
    asm volatile("rdpmc" : "=a"(*low), "=d"(*high) : "c"(idx));
}
void mini_ts_cp(unsigned int *low,unsigned int *high){
    asm volatile("rdtscp" : "=a"(*low), "=d"(*high) : : "ecx");
}
SRC
sed 's/mini_/gnu_/g' "$w/counter.i" >"$w/gnu.c"
cc -O0 -c -x c "$w/gnu.c" -o "$w/gnu.o"
"$minic" --target=x86_64-linux -S "$w/counter.i" -o "$w/mini.s"
cc -c -x assembler "$w/mini.s" -o "$w/mini.o"
for op in rdtsc rdpmc rdtscp;do
 grep -Fq "  $op" "$w/mini.s"
 objdump -d "$w/mini.o" | grep -Eq "[[:space:]]$op([[:space:]]|$)"
 objdump -d "$w/gnu.o" | grep -Eq "[[:space:]]$op([[:space:]]|$)"
done
cat >"$w/main.c" <<'SRC'
#include <stdio.h>
extern void mini_tick32(unsigned int*,unsigned int*);
extern void gnu_tick32(unsigned int*,unsigned int*);
extern void mini_tick64(unsigned long*,unsigned long*);
int main(void){
    unsigned int a=0,b=0,c=0,d=0;
    unsigned long e=0,f=0;
    mini_tick32(&a,&b);gnu_tick32(&c,&d);mini_tick64(&e,&f);
    if(!(a|b)||!(c|d)||!(e|f))return 1;
    puts("MINIC_X86_COUNTER_RUNTIME=PASS");
    return 42;
}
SRC
cc -O0 -no-pie "$w/main.c" "$w/gnu.c" "$w/mini.o" -o "$w/run"
rc=0;"$w/run" || rc=$?
test "$rc" -eq 42
sed 's/"rdtsc"/"rdtsc; hlt"/' "$w/counter.i" >"$w/bad-op.i"
sed 's/"=a"(\*low)/"=r"(*low)/' "$w/counter.i" >"$w/bad-reg.i"
sed 's/"ecx"/"ebx"/' "$w/counter.i" >"$w/bad-saved.i"
for case in bad-op bad-reg bad-saved;do
 if "$minic" --target=x86_64-linux -S "$w/$case.i" -o "$w/$case.s" >/dev/null 2>&1;then
  echo "unsafe counter $case accepted" >&2;exit 1
 fi
 test ! -e "$w/$case.s"
done
echo 'MINIC_X86_LINUX_COUNTER=PASS forms=3 wide=1 GNU_AS=1 native=1 negative=3'
