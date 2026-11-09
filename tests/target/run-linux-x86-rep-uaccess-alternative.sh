#!/bin/sh
# Scoped typed-MachineIR admission, GNU AS and negative validation for the
# original Linux REP MOVSB / REP STOSB private alternative patch records.
set -eu

minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

"$host_cc" -E -P \
    "$source_root/tests/target/x86_64_linux_smap_rep_alternative.c" \
    -o "$workdir/rep.i"
"$minic_cc" --target=x86_64-linux -S "$workdir/rep.i" -o "$workdir/rep.s"
"$host_cc" -c -x assembler "$workdir/rep.s" -o "$workdir/rep.o"

readelf -r "$workdir/rep.o" > "$workdir/rep.reloc"
readelf -SW "$workdir/rep.o" > "$workdir/rep.sections"
objcopy --dump-section .altinstructions="$workdir/rep.alt" "$workdir/rep.o"
features=$(od -An -tx1 -v "$workdir/rep.alt" | tr -d '[:space:]')

for section in .altinstructions .altinstr_replacement __ex_table \
               .rela.altinstructions .rela.altinstr_replacement .rela__ex_table
do
    if ! grep -Fq "$section" "$workdir/rep.sections"; then
        printf 'FAIL: REP alternative ELF section %s missing\n' "$section" >&2
        exit 1
    fi
done
for symbol in rep_movs_alternative rep_stos_alternative; do
    if ! grep -Fq "$symbol" "$workdir/rep.reloc"; then
        printf 'FAIL: REP alternative private CALL %s relocation lost\n' "$symbol" >&2
        exit 1
    fi
done
for feature in 44020100 8b010100; do
    if ! printf '%s' "$features" | grep -Fq "$feature"; then
        printf 'FAIL: REP alternative feature %s missing\n' "$feature" >&2
        exit 1
    fi
done

# A plausible instruction still must not bypass source admission once its
# replacement or fixed stack dependency has been altered.
sed 's/rep movsb/rep stosb/g' "$workdir/rep.i" > "$workdir/bad-movs.i"
sed 's/rep_stos_alternative/rep_stos_mismatched/g' \
    "$workdir/rep.i" > "$workdir/bad-stos.i"
sed 's/asm("rsp")/asm("r12")/g' "$workdir/rep.i" > "$workdir/bad-rsp.i"
for bad in bad-movs bad-stos bad-rsp; do
    if "$minic_cc" --target=x86_64-linux -S "$workdir/$bad.i" \
        -o "$workdir/$bad.s" >/dev/null 2>&1; then
        printf 'FAIL: REP alternative invalid %s source accepted\n' "$bad" >&2
        exit 1
    fi
done
printf 'PASS: REP MOVSB/STOSB typed MachineIR, GNU AS, ELF patch records, negatives\n'
