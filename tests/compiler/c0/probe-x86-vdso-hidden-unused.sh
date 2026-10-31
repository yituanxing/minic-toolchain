#!/bin/sh
# Exact GNU-AS/LD x86-64 vDSO-style visibility differential.  Unused hidden
# declarations must not create undefined symbols that prevent a shared link.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${1:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-gcc}
readelf_bin=${READELF:-readelf}
source_file="$root/tests/compiler/c0/x86_vdso_hidden_unused.c"
owner_file="$root/tests/compiler/c0/x86_vdso_hidden_unused_owner.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$host_cc" -E -P -std=gnu11 "$source_file" -o "$work/owner.i"
"$host_cc" -fPIC -O0 -c -x c "$work/owner.i" -o "$work/gnu.o"
"$minic_cc" --target=x86_64-linux -fPIC -S "$work/owner.i" -o "$work/minic.s"
"$host_cc" -fPIC -c -x assembler "$work/minic.s" -o "$work/minic.o"
"$host_cc" -fPIC -c "$owner_file" -o "$work/runtime.o"

"$readelf_bin" -Ws "$work/gnu.o" > "$work/gnu.symbols"
"$readelf_bin" -Ws "$work/minic.o" > "$work/minic.symbols"
"$readelf_bin" -Wr "$work/gnu.o" > "$work/gnu.reloc"
"$readelf_bin" -Wr "$work/minic.o" > "$work/minic.reloc"
for variant in gnu minic; do
    if grep -Fq vdso_hidden_unused_data "$work/$variant.symbols"; then
        echo "FAIL: $variant emitted a spurious undefined hidden vDSO symbol" >&2
        exit 1
    fi
    grep -E 'GLOBAL[[:space:]]+HIDDEN[[:space:]]+UND[[:space:]]+vdso_hidden_required_data$' \
        "$work/$variant.symbols" >/dev/null
    grep -E 'GLOBAL[[:space:]]+HIDDEN[[:space:]]+UND[[:space:]]+vdso_hidden_required_function$' \
        "$work/$variant.symbols" >/dev/null
    for symbol in vdso_hidden_required_data vdso_hidden_required_function; do
        grep -E "R_X86_64_PC32[[:space:]]+[^[:space:]]+[[:space:]]+$symbol" \
            "$work/$variant.reloc" >/dev/null
        if grep -E "R_X86_64_.*GOT.*[[:space:]]+$symbol" \
            "$work/$variant.reloc" >/dev/null; then
            echo "FAIL: $variant used preemptible GOT for hidden $symbol" >&2
            exit 1
        fi
    done
    grep -E 'R_X86_64_(REX_GOTPCRELX|GOTPCRELX|GOTPCREL)[[:space:]]+[^[:space:]]+[[:space:]]+vdso_default_preemptible_data' \
        "$work/$variant.reloc" >/dev/null
    "$host_cc" -shared -fPIC "$work/$variant.o" "$work/runtime.o" \
        -o "$work/$variant.so"
    "$host_cc" -no-pie "$work/$variant.o" "$work/runtime.o" \
        -o "$work/$variant.run"
    "$work/$variant.run"
done

echo 'X86_VDSO_HIDDEN_UNUSED=PASS GNU_AS=1 GNU_LD=1 shared=1 native_runtime=1 unused_hidden_undefined=0 hidden_PC32=2 default_GOT=1'
