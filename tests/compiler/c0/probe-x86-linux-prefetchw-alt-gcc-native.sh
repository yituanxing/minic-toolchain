#!/usr/bin/env bash
# The frozen Linux 6.6.143 GNU ALTERNATIVE prefetchw() template, not an
# invented prefetch instruction: both real ELF patching sections required.
set -Eeuo pipefail
: "${MINIC_CC:?exact minic-cc required}"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat >"$d/source.i" <<'SRC'
__attribute__((noinline)) void mini_linux_prefetchw(const void *x) {
 asm __inline volatile(
  "# ALT: oldnstr\n"
  "661:\n\t" "prefetcht0 %P1" "\n662:\n"
  "# ALT: padding\n"
  ".skip -(((" "665""1""f-""664""1""f" ")-(" "662b-661b" ")) > 0) * "
  "((" "665""1""f-""664""1""f" ")-(" "662b-661b" ")),0x90\n"
  "663" ":\n"
  ".pushsection .altinstructions,\"a\"\n"
  " .long 661b - .\n"
  " .long " "664""1""f - .\n"
  " .4byte " "( 6*32+ 8)" "\n"
  " .byte " "663""b-661b" "\n"
  " .byte " "665""1""f-664""1""f" "\n"
  ".popsection\n"
  ".pushsection .altinstr_replacement, \"ax\"\n"
  "# ALT: replacement " "1" "\n"
  "664""1"":\n\t" "prefetchw %P1" "\n"
  "665""1" ":\n"
  ".popsection\n"
  : : "i"(0), "m"(*(const char*)x));
}
SRC
sed 's/mini_linux_prefetchw/gnu_linux_prefetchw/g' "$d/source.i" >"$d/gnu.i"
cat >"$d/main.c" <<'SRC'
extern void mini_linux_prefetchw(const void *);
extern void gnu_linux_prefetchw(const void *);
int main(void) {
 static unsigned char buffer[256] __attribute__((aligned(64)));
 for(int i=0;i<1000;++i){
   buffer[i%256]=(unsigned char)i;
   gnu_linux_prefetchw(buffer+(i%128));
   mini_linux_prefetchw(buffer+(i%128));
 }
 return 42;
}
SRC
gcc -O0 -fno-pie -c -x c "$d/gnu.i" -o "$d/gnu.o"
"$MINIC_CC" --target=x86_64-linux -S "$d/source.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
for name in .altinstructions .altinstr_replacement;do
 readelf -SW "$d/mini.o" | grep -Fq "$name"
 readelf -SW "$d/gnu.o" | grep -Fq "$name"
done
grep -Fq 'prefetcht0 (%r10)' "$d/mini.s"
grep -Fq 'prefetchw (%r10)' "$d/mini.s"
grep -Fq '6641:' "$d/mini.s"
gcc -static -O0 -no-pie "$d/main.c" "$d/gnu.o" "$d/mini.o" -o "$d/joint.elf"
set +e
timeout 45s "$d/joint.elf" >"$d/stdout" 2>"$d/stderr";rc=$?
set -e
if ((rc!=42));then
 echo "X86_LINUX_PREFETCHW_ALT=FAIL rc=$rc" >&2
 cat "$d/stderr" >&2;exit 1
fi
sed 's/prefetchw %P1/prefetchnta %P1/' "$d/source.i" >"$d/bad-instruction.i"
sed 's/( 6\*32+ 8)/( 6*32+ 9)/' "$d/source.i" >"$d/bad-feature.i"
for form in instruction feature;do
 if "$MINIC_CC" --target=x86_64-linux -S "$d/bad-$form.i" -o "$d/bad-$form.s" 2>"$d/bad-$form.err";then
   echo "X86_LINUX_PREFETCHW_ALT=FAIL accepted unsafe_$form" >&2;exit 1
 fi
 test ! -e "$d/bad-$form.s"
done
echo "X86_LINUX_PREFETCHW_ALT=PASS GNU_6_6_143_SOURCE=1 native_PF0_and_PREFETCHW=1 ELF_alt_sections=2 GCC_link_runtime=1 strict_negatives=2"
