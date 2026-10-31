#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
gnu=${CC:-gcc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-x86-global-plan-abi
source="$root/tests/compiler/c0/x86_global_plan_abi.c"
mkdir -p "$work"

"$gnu" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$source"
"$gnu" -std=gnu11 -E -P -x c "$source" -o "$work/input.i"
"$minic" -S "$work/input.i" --target=x86_64-linux -o "$work/mini.s"
"$gnu" -std=gnu11 -O0 -no-pie "$source" -o "$work/gnu.elf"
"$gnu" -no-pie "$work/mini.s" -o "$work/mini.elf" \
    >"$work/mini-link.stdout" 2>"$work/mini-link.stderr"
"$work/gnu.elf"
"$work/mini.elf"

"$gnu" -std=gnu11 -O0 -c "$source" -o "$work/gnu.o"
"$gnu" -c -x assembler "$work/mini.s" -o "$work/mini.o" \
    >"$work/mini-asm.stdout" 2>"$work/mini-asm.stderr"
if grep -F 'incorrect section type' "$work/mini-asm.stderr" >/dev/null; then
    printf '%s\n' 'FAIL: MiniC emitted incorrect named ELF section type' >&2
    exit 1
fi

for owner in gnu mini; do
    readelf -SW "$work/$owner.o" >"$work/$owner.sections"
    readelf -sW "$work/$owner.o" >"$work/$owner.symbols"
    readelf -rW "$work/$owner.o" >"$work/$owner.relocs"
    awk '$3 == ".bss.minic.global" && $4 == "NOBITS" { valid = 1 }
         END { exit valid ? 0 : 1 }' "$work/$owner.sections"
    awk '$3 == ".bssish.minic.global" && $4 == "PROGBITS" { valid = 1 }
         END { exit valid ? 0 : 1 }' "$work/$owner.sections"
    test "$(grep -c 'R_X86_64_64' "$work/$owner.relocs")" -eq 8
done

for object in global_anchor global_float global_double global_pointer \
              global_nested global_pointer_grid global_flexible \
              named_bss_zero named_bssish_zero; do
    gnu_size=$(awk -v name="$object" '$NF == name { print $3; exit }' \
        "$work/gnu.symbols")
    mini_size=$(awk -v name="$object" '$NF == name { print $3; exit }' \
        "$work/mini.symbols")
    if test -z "$gnu_size" || test "$gnu_size" != "$mini_size"; then
        printf '%s\n' "FAIL: GNU/MiniC global ABI size differs for $object" >&2
        exit 1
    fi
done

invalid="$root/tests/compiler/c0/invalid_x86_bss_nonzero_initializer.c"
if "$gnu" -std=gnu11 -c "$invalid" -o "$work/gnu-invalid.o" \
    >"$work/gnu-invalid.stdout" 2>"$work/gnu-invalid.stderr"; then
    printf '%s\n' 'FAIL: GNU accepted nonzero initializer in named BSS' >&2
    exit 1
fi
"$gnu" -std=gnu11 -E -P -x c "$invalid" -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" --target=x86_64-linux -o "$work/invalid.s" \
    >"$work/mini-invalid.stdout" 2>"$work/mini-invalid.stderr"; then
    printf '%s\n' 'FAIL: MiniC accepted nonzero initializer in named BSS' >&2
    exit 1
fi

printf '%s\n' 'PASS x86-global-plan-abi gnu-native=1 minic-native=1 bss-nobits=1 bssish-progbits=1 bss-nonzero-reject=1 scalar-fp=2 symbol-sizes=9 reloc-data=8 flexible=1 nested-reloc=1'
