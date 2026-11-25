#!/usr/bin/env bash
# Linux x86 PM_TRACE_RESUME's exact GNU extended-asm template is not just a
# pointer assignment: it publishes ten-byte records in ELF .tracedata with
# absolute relocations to the compile-time __FILE__ string.
set -Eeuo pipefail
cc_bin=$1
test -x "$cc_bin"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/mini.i" <<'SRC'
typedef unsigned long u64;
void mini_resume(u64 *a, u64 *b) {
 void *pa, *pb;
 asm volatile(" movq $1f,%0\n"
              ".section .tracedata,\"a\"\n"
              "1:\t.word %c1\n\t"
              " .quad %c2\n"
              ".previous"
              : "=r"(pa) : "i"(598), "i"("drivers/base/power/main.c"));
 asm volatile(" movq $1f,%0\n"
              ".section .tracedata,\"a\"\n"
              "1:\t.word %c1\n\t"
              " .quad %c2\n"
              ".previous"
              : "=r"(pb) : "i"(1629), "i"("drivers/base/power/main.c"));
 *a=(u64)pa; *b=(u64)pb;
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" >"$d/gnu.c"
gcc -S -O2 -fno-pie -fno-pic -x c "$d/gnu.c" -o "$d/gnu.s"
gcc -c -x assembler "$d/gnu.s" -o "$d/gnu.o"
"$cc_bin" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
for compiler in gnu mini; do
 readelf -SW "$d/$compiler.o" | grep -Eq '\] \.tracedata[[:space:]]+PROGBITS.*000014'
 readelf -SW "$d/$compiler.o" | grep -Eq '\] \.rela.tracedata[[:space:]]+RELA'
 test "$(readelf -rW "$d/$compiler.o" | grep -c 'R_X86_64_64.*\.rodata')" = 2
 test "$(readelf -rW "$d/$compiler.o" | grep -c 'R_X86_64_32S.*\.tracedata')" = 2
 objcopy --dump-section .tracedata="$d/$compiler.tracedata" "$d/$compiler.o"
 test "$(stat -c %s "$d/$compiler.tracedata")" = 20
 first=$(od -An -tu2 -j0 -N2 "$d/$compiler.tracedata" | tr -d ' ')
 second=$(od -An -tu2 -j10 -N2 "$d/$compiler.tracedata" | tr -d ' ')
 test "$first:$second" = '598:1629'
 # Each .quad has a relocation, never an immediate counterfeit pointer.
 test "$(od -An -tu8 -j2 -N8 "$d/$compiler.tracedata" | tr -d ' ')" = 0
 test "$(od -An -tu8 -j12 -N8 "$d/$compiler.tracedata" | tr -d ' ')" = 0
done
sed 's/: "=r"(pa) : "i"(598)/: "=r"(pa) : "r"(598)/' "$d/mini.i" >"$d/bad-line.i"
sed 's/: "=r"(pa) : "i"(598)/: "=m"(pa) : "i"(598)/' "$d/mini.i" >"$d/bad-output.i"
sed 's/asm volatile(" movq/asm(" movq/' "$d/mini.i" >"$d/bad-volatile.i"
sed 's/"i"("drivers\/base\/power\/main.c"));/"i"("drivers\/base\/power\/main.c") : "memory");/' "$d/mini.i" >"$d/bad-clobber.i"
for case in bad-line bad-output bad-volatile bad-clobber; do
 if "$cc_bin" --target=x86_64-linux -S "$d/$case.i" -o "$d/$case.s" >/dev/null 2>&1; then
  echo "FAIL accepted unsafe PM_TRACE_RESUME template $case" >&2
  exit 1
 fi
 test ! -e "$d/$case.s"
done
echo 'MINIC_X86_LINUX_PM_TRACEDATA_ELF=PASS GNU_GAS=1 records=2 bytes=20 reloc64=2 text32S=2 negative=4'
