#!/usr/bin/env bash
# GNU Linux 6.6 alternative.h macro fully expanded, exact ISA and ELF.
set -Eeuo pipefail
trap 'echo "MINIC_HWEIGHT_CHECK_FAIL line=$LINENO command=$BASH_COMMAND" >&2' ERR
minic=$1;test -x "$minic"
w=$(mktemp -d);trap 'rm -rf "$w"' EXIT
python3 - "$w" <<'PY'
import json,pathlib,sys
w=pathlib.Path(sys.argv[1])
def alt(bits):
 return (f"# ALT: oldnstr\n661:\n\tcall __sw_hweight{bits}\n662:\n"
     "# ALT: padding\n"
     ".skip -(((6651f-6641f)-(662b-661b)) > 0) * ((6651f-6641f)-(662b-661b)),0x90\n"
     "663:\n.pushsection .altinstructions,\"a\"\n"
     " .long 661b - .\n .long 6641f - .\n .4byte ( 4*32+23)\n"
     " .byte 663b-661b\n .byte 6651f-6641f\n.popsection\n"
     ".pushsection .altinstr_replacement, \"ax\"\n"
     "# ALT: replacement 1\n"
     f"6641:\n\tpopcnt{'l' if bits==32 else 'q'} %1, %0\n6651:\n"
     ".popsection\n")
def fixture(prefix):
 return (f"unsigned int {prefix}_h32(unsigned int v){{unsigned int out;"
      f"asm ({json.dumps(alt(32))} : \"=a\"(out) : \"D\"(v));return out;}}\n"
      f"unsigned long {prefix}_h64(unsigned long long v){{unsigned long out;"
      f"asm ({json.dumps(alt(64))} : \"=a\"(out) : \"D\"(v));return out;}}\n")
src=fixture("mini")
(w/"mini.i").write_text(src)
(w/"gnu.c").write_text(fixture("gnu"))
(w/"bad-suffix.i").write_text(src.replace("popcntq %1, %0","popcntq %1, %0; cli"))
(w/"bad-feature.i").write_text(src.replace("4*32+23","4*32+22"))
(w/"bad-reg.i").write_text(src.replace('"D"(v)','"r"(v)'))
PY
cat >"$w/helper.c" <<'SRC'
unsigned int __sw_hweight32(unsigned int x){
 unsigned int n=0;while(x){n+=(x&1U);x>>=1;}return n;
}
unsigned long __sw_hweight64(unsigned long long x){
 unsigned long n=0;while(x){n+=(unsigned long)(x&1ULL);x>>=1;}return n;
}
SRC
cat >"$w/driver.c" <<'SRC'
#include <stdio.h>
extern unsigned int mini_h32(unsigned int),gnu_h32(unsigned int);
extern unsigned long mini_h64(unsigned long long),gnu_h64(unsigned long long);
int main(void){
 for(unsigned int i=0;i<128;++i){
  unsigned int v=i*164987U+211U;
  unsigned long long x=((unsigned long long)v<<32)|
     ((unsigned long long)v^0x21f23855ULL);
  if(mini_h32(v)!=gnu_h32(v)||mini_h64(x)!=gnu_h64(x))return 1;
 }
 puts("MINIC_X86_HWEIGHT_GNU_NATIVE=PASS pairs=256");return 42;
}
SRC
"$minic" --target=x86_64-linux -S "$w/mini.i" -o "$w/mini.s"
cc -O0 -c -x c "$w/gnu.c" -o "$w/gnu.o"
cc -c -x assembler "$w/mini.s" -o "$w/mini.o"
readelf -SW "$w/mini.o" | grep -Fq '.altinstructions'
readelf -SW "$w/mini.o" | grep -Fq '.altinstr_replacement'
grep -Fq '.4byte 151' "$w/mini.s"
cc -O0 -no-pie "$w/driver.c" "$w/helper.c" "$w/gnu.o" "$w/mini.o" -o "$w/run"
rc=0;"$w/run" || rc=$?
test "$rc" -eq 42
for bad in bad-suffix bad-feature bad-reg;do
 if "$minic" --target=x86_64-linux -S "$w/$bad.i" -o "$w/$bad.s" >/dev/null 2>&1;then
  echo "FAIL unsafe x86 ALTERNATIVE $bad" >&2;exit 1
 fi
 test ! -e "$w/$bad.s"
done
echo 'MINIC_X86_HWEIGHT_ALTERNATIVE=PASS widths=2 feature=151 pairs=256 negative=3'
