#!/usr/bin/env bash
# Real original Linux 6.6.143 arm64 asm/io.h dependency family in readl.
set -Eeuo pipefail
mini="$1"
test -x "$mini"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat > "$d/io.i" <<'SRC'
unsigned long mini_dependency(unsigned long value) {
 unsigned long tmp;
 asm volatile("eor	%0, %1, %1\n" "cbnz	%0, ."
              : "=r"(tmp) : "r"(value) : "memory");
 return tmp;
}
unsigned long mini_readl_dependency(unsigned int val) {
 unsigned long tmp;
 asm volatile("eor	%0, %1, %1\n" "cbnz	%0, ."
              : "=r"(tmp) : "r"((unsigned long)val) : "memory");
 return val + tmp;
}
SRC
"$mini" --target=aarch64-linux -S "$d/io.i" -o "$d/mini.s"
test "$(grep -Fc '  eor x9, x10, x10' "$d/mini.s")" -eq 2
test "$(grep -Fc '  cbnz x9, .' "$d/mini.s")" -eq 2
for bad in second-instruction extra-instruction wrong-constraint missing-clobber; do
 case "$bad" in
  second-instruction) sed 's/cbnz/cbz/g' "$d/io.i" > "$d/bad.i" ;;
  extra-instruction) sed 's/cbnz/cbnz/g;s/%0, \./%0, .; nop/' "$d/io.i" > "$d/bad.i" ;;
  wrong-constraint) sed 's/=r/=m/g' "$d/io.i" > "$d/bad.i" ;;
  missing-clobber) sed 's/ : "memory");/ );/g' "$d/io.i" > "$d/bad.i" ;;
 esac
 if "$mini" --target=aarch64-linux -S "$d/bad.i" -o "$d/bad.s" >"$d/err" 2>&1;then
  echo "BAD_AARCH64_IO_DEPENDENCY_ADMITTED=$bad" >&2;exit 1
 fi
 test ! -e "$d/bad.s"
done
if [[ "${MINIC_ARM_IO_DEPENDENCY_SOURCE_ONLY:-0}" != 1 ]]; then
 aarch64-linux-gnu-gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
 sed 's/mini_/gnu_/g' "$d/io.i" > "$d/gnu.c"
 aarch64-linux-gnu-gcc -c -O0 "$d/gnu.c" -o "$d/gnu.o"
 cat > "$d/main.c" <<'SRC'
extern unsigned long mini_dependency(unsigned long),gnu_dependency(unsigned long);
extern unsigned long mini_readl_dependency(unsigned int),gnu_readl_dependency(unsigned int);
int main(void) {
 static const unsigned long seeds[]={
  0UL,1UL,2UL,0x5555555555555555UL,0xdeadbeefabcd0123UL,~0UL};
 for(unsigned i=0;i<sizeof(seeds)/sizeof(seeds[0]);++i) {
  unsigned long a=mini_dependency(seeds[i]),b=gnu_dependency(seeds[i]);
  if(a!=b || a!=0UL)return 1;
  unsigned long c=mini_readl_dependency((unsigned int)seeds[i]);
  unsigned long d=gnu_readl_dependency((unsigned int)seeds[i]);
  if(c!=d)return 2;
 }
 return 0;
}
SRC
 aarch64-linux-gnu-gcc -static -O0 "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
 qemu-aarch64 "$d/run"
fi
echo "MINIC_AARCH64_LINUX_IO_DEPENDENCY=PASS original_readl=1 genuine_two_opcode=1 GNU_diff=1 negatives=4"
