#!/bin/sh
set -eu
: "${MINIC_CC:?set MINIC_CC to the built minic-cc}"
host_cc=${HOST_CC:-cc}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/c0/x86_pat_clflushopt_alternative.c"

"$host_cc" -E -P -x c "$source" -o "$work/source.i"
"$MINIC_CC" --target=x86_64-linux -S "$work/source.i" -o "$work/minic.s"
"$host_cc" -c -x assembler "$work/minic.s" -o "$work/minic.o"
"$host_cc" -no-pie -x assembler "$work/minic.s" -o "$work/minic.bin"
"$host_cc" -std=gnu11 -O0 -no-pie "$source" -o "$work/gnu.bin"
for name in minic gnu; do
    set +e
    "$work/$name.bin"
    status=$?
    set -e
    if [ "$status" -ne 42 ]; then
        echo "FAIL: $name PAT GNU ALT runtime status=$status" >&2
        exit 1
    fi
done
readelf -SW "$work/minic.o" | grep -F '.altinstructions' >/dev/null
readelf -SW "$work/minic.o" | grep -F '.altinstr_replacement' >/dev/null
readelf -rW "$work/minic.o" | grep -F '.rela.altinstructions' >/dev/null
grep -F '.4byte (9*32+23)' "$work/minic.s" >/dev/null
echo 'PASS: PAT CLFLUSHOPT GNU ALT/ELF relocation + native GNU/MiniC exit=42'

sed 's/(9\*32+23)/(9*32+24)/' "$work/source.i" >"$work/bad.i"
if "$MINIC_CC" --target=x86_64-linux -S "$work/bad.i" \
    -o "$work/bad.s" >"$work/bad.stdout" 2>"$work/bad.stderr"; then
    echo 'FAIL: unknown PAT feature bit compiled' >&2
    exit 1
fi
echo 'PASS: unknown PAT ALT feature bit fail-closed'
