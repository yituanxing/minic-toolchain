#!/usr/bin/env bash
set -Eeuo pipefail
cc=${1:?MiniC binary};test -x "$cc"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat > "$d/clock.i" <<'SRC'
unsigned long long read_clock(void) {
 unsigned long low,high;
 asm volatile("# ALT: oldinstr2\n661:\n\trdtsc\n662:\n# ALT: padding2\n.skip -((((6651f-6641f) ^ (((6651f-6641f) ^ (6652f-6642f)) & -(-((6651f-6641f) < (6652f-6642f))))) - (662b-661b)) > 0) * (((6651f-6641f) ^ (((6651f-6641f) ^ (6652f-6642f)) & -(-((6651f-6641f) < (6652f-6642f))))) - (662b-661b)), 0x90\n663:\n.pushsection .altinstructions,\"a\"\n .long 661b - .\n .long 6641f - .\n .4byte (20*32+ 2)\n .byte 663b-661b\n .byte 6651f-6641f\n .long 661b - .\n .long 6642f - .\n .4byte ( 1*32+27)\n .byte 663b-661b\n .byte 6652f-6642f\n.popsection\n.pushsection .altinstr_replacement, \"ax\"\n# ALT: replacement 1\n6641:\n\tlfence; rdtsc\n6651:\n# ALT: replacement 2\n6642:\n\trdtscp\n6652:\n.popsection\n" : "=a"(low), "=d"(high) :: "ecx");
 return low | high << 32;
}
int main(void) {return read_clock()?42:1;}
SRC
"$cc" --target=x86_64-linux -S "$d/clock.i" -o "$d/mini.s"
grep -Fq '.altinstructions' "$d/mini.s"
grep -Fq '.altinstr_replacement' "$d/mini.s"
if [[ "${MINIC_SOURCE_ONLY:-0}" != 1 ]]; then
 gcc -static -x assembler "$d/mini.s" -o "$d/mini"
 gcc -static -x c "$d/clock.i" -o "$d/gnu"
 for exe in mini gnu;do rc=0;"$d/$exe" || rc=$?;test "$rc" -eq 42;done
fi
sed 's/lfence; rdtsc/lfence; hlt/' "$d/clock.i" > "$d/unsafe.i"
if "$cc" --target=x86_64-linux -S "$d/unsafe.i" -o "$d/unsafe.s" >/dev/null 2>&1;then
 echo 'unsafe ordered-clock alternative accepted' >&2;exit 1
fi
test ! -e "$d/unsafe.s"
echo 'X86_ORIGINAL_ORDERED_TSC=PASS exact_bytes=652 alt_relocations=2 negative=rejected'
