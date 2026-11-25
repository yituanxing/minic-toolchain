#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/x86_linux_zero_extent_record_arrays.c" \
    -o "$work/gnu.i"
"$host_cc" -std=gnu11 -O0 -c "$work/gnu.i" -o "$work/gnu.o"
"$minic" --target=x86_64-linux -S "$work/gnu.i" -o "$work/minic.s"
"$host_cc" -x assembler -c "$work/minic.s" -o "$work/minic.o"

for name in supported_gss_krb5_enctypes config_table nested_config; do
    grep -Fq ".size $name,0" "$work/minic.s"
    for object in gnu minic; do
        "${NM:-nm}" -S "$work/$object.o" |
            grep -Eq " r $name\$"
    done
done

printf '%s\n' 'PASS compiler/c0/x86_zero_record_array gss=0 hda=0 nested=0 gnu_elf=1 minic_elf=1'
