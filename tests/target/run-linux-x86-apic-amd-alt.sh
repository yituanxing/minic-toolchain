#!/bin/sh
set -eu
minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

for fixture in x86_64_linux_apic_amd_alt x86_64_linux_alternative_wrmsr; do
    input="$source_root/tests/target/$fixture.c"
    "$host_cc" -E -P "$input" -o "$workdir/$fixture.i"
    "$minic_cc" -S "$workdir/$fixture.i" --target=x86_64-linux \
        -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/$fixture.s"
    "$host_cc" -c -x assembler "$workdir/$fixture.s" \
        -o "$workdir/$fixture.o"
    readelf -r "$workdir/$fixture.o" > "$workdir/$fixture.relocs"
    if ! grep -q "Relocation section '.rela.altinstructions'" \
        "$workdir/$fixture.relocs"; then
        printf 'FAIL: missing %s GNU APIC patch relocations\n' "$fixture" >&2
        exit 1
    fi
done

readelf -r "$workdir/x86_64_linux_apic_amd_alt.o" | \
    grep -c R_X86_64_PC32 > "$workdir/apic-count"
if [ "$(sed -n '1p' "$workdir/apic-count")" -ne 4 ]; then
    printf 'FAIL: APIC alternatives need 4 section PC32 fixups\n' >&2
    exit 1
fi
objdump -d -j .altinstr_replacement \
    "$workdir/x86_64_linux_apic_amd_alt.o" > "$workdir/replacement"
if ! grep -Eq 'xchg.*%eax,\(%r10\)' "$workdir/replacement" ||
   ! grep -Eq 'div.*%r10d' "$workdir/replacement"; then
    printf 'FAIL: AMD APIC replacement machine instructions differ\n' >&2
    exit 1
fi
objdump -s -j .altinstructions \
    "$workdir/x86_64_linux_alternative_wrmsr.o" > "$workdir/wrmsr-bytes"
if ! grep -q 'b4000000' "$workdir/wrmsr-bytes" ||
   ! grep -q 'f500' "$workdir/wrmsr-bytes"; then
    printf 'FAIL: WRMSR 180 and 245 feature records differ\n' >&2
    exit 1
fi

# Negative controls must fail admission/selection; invalid ELF instruction
# alternatives must not be printed as arbitrary GNU inline assembler.
sed 's/(22\*32 + (5))/(22*32 + (6))/' \
    "$source_root/tests/target/x86_64_linux_apic_amd_alt.c" | \
    "$host_cc" -E -P -x c - -o "$workdir/apic-bad.i"
if "$minic_cc" -S "$workdir/apic-bad.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/apic-bad.s" \
    > "$workdir/apic-bad.log" 2>&1; then
    printf 'FAIL: mutated AMD APIC patch feature accepted\n' >&2
    exit 1
fi
sed 's/\[feature\] "i" (245)/[feature] "i" (4294967296UL)/' \
    "$source_root/tests/target/x86_64_linux_alternative_wrmsr.c" | \
    "$host_cc" -E -P -x c - -o "$workdir/wrmsr-bad.i"
if "$minic_cc" -S "$workdir/wrmsr-bad.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/wrmsr-bad.s" \
    > "$workdir/wrmsr-bad.log" 2>&1; then
    printf 'FAIL: out-of-range WRMSR patch feature accepted\n' >&2
    exit 1
fi
printf 'PASS: AMD APIC two forms, ELF PC32, WRMSR 180/245, negatives\n'
