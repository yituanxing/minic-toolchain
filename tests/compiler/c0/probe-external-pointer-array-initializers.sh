#!/bin/sh
set -eu

: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

valid="$root/tests/compiler/c0/external_pointer_array_initializer.c"
${CC:-cc} -std=gnu11 -Werror -O0 "$valid" -o "$work/gnu"
${CC:-cc} -E -P -x c "$valid" -o "$work/valid.i"
"$MINIC_CC" --target=x86_64-linux -S "$work/valid.i" -o "$work/valid.s"
${CC:-cc} -no-pie "$work/valid.s" -o "$work/minic"
"$work/gnu"
"$work/minic"

for name in external_pointer_array_implicit_address_invalid \
            external_pointer_array_inferred_implicit_address_invalid; do
    invalid="$root/tests/compiler/c0/$name.c"
    if ${CC:-cc} -std=gnu11 -pedantic-errors -fsyntax-only "$invalid" \
            >"$work/$name.gcc.log" 2>&1; then
        echo "GCC unexpectedly accepted $name" >&2
        exit 1
    fi
    ${CC:-cc} -E -P -x c "$invalid" -o "$work/$name.i"
    if "$MINIC_CC" --target=x86_64-linux -S "$work/$name.i" \
            -o "$work/$name.s" >"$work/$name.minic.log" 2>&1; then
        echo "MiniC unexpectedly accepted $name" >&2
        exit 1
    fi
done

echo 'EXTERNAL_POINTER_ARRAY_INITIALIZER=PASS gcc_runtime=1 minic_runtime=1 fixed_negative=1 inferred_negative=1'
