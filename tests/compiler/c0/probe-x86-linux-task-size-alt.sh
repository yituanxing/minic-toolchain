#!/usr/bin/env bash
# MiniC and GNU AS must retain the complete TASK_SIZE_MAX feature patch.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
fixture=${BASH_SOURCE[0]%/*}/x86_linux_task_size_alt.c
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

gcc -std=gnu11 -E -P "$fixture" -o "$work/mini.i"
"$mini" --target=x86_64-linux -S "$work/mini.i" -o "$work/mini.s"
as --64 -o "$work/mini.o" "$work/mini.s"
gcc -O0 -c "$fixture" -o "$work/gnu.o"

for obj in "$work/mini.o" "$work/gnu.o"; do
    readelf -SW "$obj" | grep -Eq '\.altinstructions[[:space:]]+PROGBITS.*00000e'
    readelf -SW "$obj" | grep -Eq '\.altinstr_replacement[[:space:]]+PROGBITS.*00000a'
    readelf -rW "$obj" | grep -A 4 "^Relocation section '.rela.altinstructions'" |
        grep -Eq 'contains 2 entries'
    readelf -x .altinstructions "$obj" | grep -qi '1002'
done

sed 's/16\*32+16/16*32+17/' "$fixture" > "$work/bad-feature.c"
sed 's/1UL << 56/1UL << 55/' "$fixture" > "$work/bad-ceiling.c"
sed 's/: "=r" (ret)/: "=m" (ret)/' "$fixture" > "$work/bad-output.c"
for bad in bad-feature bad-ceiling bad-output; do
    gcc -std=gnu11 -E -P "$work/$bad.c" -o "$work/$bad.i"
    if "$mini" --target=x86_64-linux -S "$work/$bad.i" \
            -o "$work/$bad.s" >/dev/null 2>"$work/$bad.err"; then
        echo "FAIL: TASK_SIZE_MAX $bad was admitted" >&2
        exit 1
    fi
done
echo 'MINIC_X86_TASK_SIZE_ALT=PASS GNU_AS=2 alt_record=14 replacement=10 relocs=2 negatives=3'
