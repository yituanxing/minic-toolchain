#!/usr/bin/env bash
# x86-64 descriptor-table instructions are privileged. Validate only assembly
# syntax, ELF instructions and source admission; never execute these objects.
set -Eeuo pipefail
mini=${1:?MiniC compiler required}
test -x "$mini"
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT

source="$repo/tests/target/x86_64_linux_lidt_packed_descriptor.c"
gcc -E -P "$source" -o "$d/valid.i"
"$mini" -S "$d/valid.i" --target=x86_64-linux -o "$d/minic.s"
gcc -x assembler -c "$d/minic.s" -o "$d/minic.o"
gcc -c -O0 "$source" -o "$d/gnu.o"
objdump -dr "$d/minic.o" > "$d/minic.dump"
objdump -dr "$d/gnu.o" > "$d/gnu.dump"
for instruction in lidt lgdt sgdt sidt; do
    grep -Eq "[[:space:]]${instruction}[[:space:]]" "$d/minic.dump"
    grep -Eq "[[:space:]]${instruction}[[:space:]]" "$d/gnu.dump"
done
readelf -h "$d/minic.o" | grep -Eq 'Machine:[[:space:]]*Advanced Micro Devices X86-64'

for invalid in bad_size bad_layout; do
    source="$repo/tests/target/x86_64_linux_descriptor_${invalid}.c"
    gcc -E -P "$source" -o "$d/${invalid}.i"
    if "$mini" -S "$d/${invalid}.i" --target=x86_64-linux \
            -o "$d/${invalid}.s" >"$d/${invalid}.stdout" 2>"$d/${invalid}.stderr"; then
        echo "X86_DESCRIPTOR_FAIL=${invalid}_accepted" >&2
        exit 1
    fi
    test ! -e "$d/${invalid}.s"
done

echo 'MINIC_X86_DESCRIPTOR_ELF=PASS GNU_AS=4 GNU_GCC=4 MiniC=4 negative=2 privileged_not_executed=1'
