#!/usr/bin/env bash
# Exact GNU Linux asm/msr.h 6.6 privileged MSR with exception table.
set -Eeuo pipefail
cc_bin=$1;test -x "$cc_bin"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat >"$d/msr.i" <<'SRC'
typedef unsigned int u32;typedef unsigned long u64;
void mini_rd(u32 msr,u64 *lo,u64 *hi){
 asm volatile("1: rdmsr\n2:\n"
 " .pushsection \"__ex_table\",\"a\"\n"
 " .balign 4\n"
 " .long (1b) - .\n"
 " .long (2b) - .\n"
 " .long 9\n"
 " .popsection\n"
 : "=a"(*lo), "=d"(*hi) : "c"(msr));
}
void mini_wr(u32 msr,u32 lo,u32 hi){
 asm volatile("1: wrmsr\n2:\n"
 " .pushsection \"__ex_table\",\"a\"\n"
 " .balign 4\n"
 " .long (1b) - .\n"
 " .long (2b) - .\n"
 " .long 8\n"
 " .popsection\n"
 : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}
void mini_mce_wr(u32 msr,u32 lo,u32 hi){
 asm volatile("1: wrmsr\n2:\n"
 " .pushsection \"__ex_table\",\"a\"\n"
 " .balign 4\n"
 " .long (1b) - .\n"
 " .long (2b) - .\n"
 " .long 12\n"
 " .popsection\n"
 : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}
SRC
sed 's/mini_/gnu_/g' "$d/msr.i" >"$d/gnu.c"
gcc -c -O0 -x c "$d/gnu.c" -o "$d/gnu.o"
"$cc_bin" --target=x86_64-linux -S "$d/msr.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
readelf -SW "$d/mini.o" | grep -Fq '__ex_table'
grep -Fq '.long 9' "$d/mini.s"
grep -Fq '.long 8' "$d/mini.s"
grep -Fq '.long 12' "$d/mini.s"
for opcode in rdmsr wrmsr;do
 objdump -d "$d/gnu.o" | grep -Eq "[[:space:]]$opcode([[:space:]]|$)"
 objdump -d "$d/mini.o" | grep -Eq "[[:space:]]$opcode([[:space:]]|$)"
done
sed 's/ .long 9/ .long 10/' "$d/msr.i" >"$d/bad-fixup.i"
sed 's/"=a"(\*lo)/"=r"(*lo)/' "$d/msr.i" >"$d/bad-reg.i"
sed 's/"1: wrmsr/"1: wrmsr; hlt/' "$d/msr.i" >"$d/bad-extra.i"
sed 's/ .long 12/ .long 14/' "$d/msr.i" >"$d/bad-mce-type.i"
sed '/void mini_mce_wr/,/^}/s/ : "memory");/);/' "$d/msr.i" >"$d/bad-mce-clobber.i"
for case in bad-fixup bad-reg bad-extra bad-mce-type bad-mce-clobber;do
 if "$cc_bin" --target=x86_64-linux -S "$d/$case.i" \
  -o "$d/$case.s" >/dev/null 2>&1;then
  echo "FAIL accepted unsafe privileged MSR $case" >&2;exit 1
 fi
 test ! -e "$d/$case.s"
done
echo 'MINIC_X86_LINUX_MSR_EXTABLE=PASS forms=3 fixup_types=8,9,12 GNU_GAS=1 negative=5'
