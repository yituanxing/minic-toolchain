#!/bin/sh
# Compile-only cacheline alternatives and all ten privileged zero-input forms.
set -eu
minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

for fixture in \
    tests/target/x86_64_linux_idle_clflush_alt.c \
    tests/compiler/c0/x86_pat_clflushopt_alternative.c \
    tests/target/x86_64_linux_privileged_zero.c
do
    name=$(basename "$fixture" .c)
    "$host_cc" -E -P "$source_root/$fixture" -o "$workdir/$name.i"
    "$minic_cc" -S "$workdir/$name.i" --target=x86_64-linux \
        -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/$name.s"
    "$host_cc" -c -x assembler "$workdir/$name.s" -o "$workdir/$name.o"
done

for name in x86_64_linux_idle_clflush_alt x86_pat_clflushopt_alternative
do
    readelf -rW "$workdir/$name.o" > "$workdir/$name.reloc"
    readelf -SW "$workdir/$name.o" > "$workdir/$name.sections"
    for section in .altinstructions .altinstr_replacement
    do
        if ! grep -Fq "$section" "$workdir/$name.sections"; then
            echo "FAIL: cache alternative $name lacks $section" >&2
            exit 1
        fi
    done
    if ! grep -Fq 'R_X86_64_PC32' "$workdir/$name.reloc"; then
        echo "FAIL: cache alternative $name lacks PREL32 patch reloc" >&2
        exit 1
    fi
done

objdump -d "$workdir/x86_64_linux_privileged_zero.o" \
    > "$workdir/privileged.dump"
for mnemonic in cli sti hlt wbinvd swapgs serialize out int3
do
    if ! grep -Fq "$mnemonic" "$workdir/privileged.dump"; then
        echo "FAIL: privileged hardware instruction $mnemonic absent" >&2
        exit 1
    fi
done

sed 's/(22\*32 + (7))/(22*32 + (8))/' \
    "$workdir/x86_64_linux_idle_clflush_alt.i" > "$workdir/invalid-feature.i"
if "$minic_cc" -S "$workdir/invalid-feature.i" --target=x86_64-linux \
    -o "$workdir/invalid-feature.s" >/dev/null 2>&1; then
    echo 'FAIL: altered Linux idle CPU feature accepted' >&2
    exit 1
fi
printf 'PASS: Linux idle/PAT cache alternatives and ten privileged zero-input forms\n'
