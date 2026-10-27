#!/usr/bin/env bash
# One GNU basic file-scope asm source must survive all three existing
# native GNU object emitters without rewriting target ISA text.
set -Eeuo pipefail
test "$#" -eq 2
: "${MINIC_CC:?set MINIC_CC to the strict compiler}"
target=$1
cross_cc=$2
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/original.i" <<'SRC'
__asm__(".pushsection .rodata\n"
        ".globl minic_original_source_asm_marker\n"
        "minic_original_source_asm_marker:\n"
        ".byte 42\n"
        ".popsection\n");
int minic_source_asm_entry(void) { return 42; }
SRC
"$cross_cc" -c -x c "$work/original.i" -o "$work/original.gnu.o"
"$MINIC_CC" --target="$target-linux" -S "$work/original.i" -o "$work/original.minic.s"
test -s "$work/original.minic.s"
grep -Fq '.globl minic_original_source_asm_marker' "$work/original.minic.s"
grep -Fq 'minic_original_source_asm_marker:' "$work/original.minic.s"
grep -Fq '.byte 42' "$work/original.minic.s"
marker=$(grep -n -F 'minic_original_source_asm_marker:' "$work/original.minic.s" | head -1 | cut -d: -f1)
entry=$(grep -n -F 'minic_source_asm_entry:' "$work/original.minic.s" | head -1 | cut -d: -f1)
test "$marker" -lt "$entry"
"$cross_cc" -c -x assembler "$work/original.minic.s" -o "$work/original.minic.o"
for name in original.gnu original.minic; do
    readelf -s "$work/$name.o" | grep -Fq 'minic_original_source_asm_marker'
    readelf -s "$work/$name.o" | grep -Fq 'minic_source_asm_entry'
done
# Architecture-specific machine syntax is still owned by real GNU AS.
echo "MINIC_FILE_ASM_GNU_AS=PASS target=$target original_gnu=1 original_bytes=1 sections=1 symbols=2"
