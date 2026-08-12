#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug/extern-typedef-array-object"}
mkdir -p "$work"

preprocess() {
    name=$1
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
}

preprocess extern_typedef_array_object
"$minic" -S "$work/extern_typedef_array_object.i" -o "$work/extern_typedef_array_object.s"
grep -F "  la a0, irq_default_affinity" "$work/extern_typedef_array_object.s" >/dev/null
grep -F "  la a0, matrix" "$work/extern_typedef_array_object.s" >/dev/null
grep -F "  la a0, values" "$work/extern_typedef_array_object.s" >/dev/null
grep -F "  li a0, 48" "$work/extern_typedef_array_object.s" >/dev/null
grep -F "  li a0, 12" "$work/extern_typedef_array_object.s" >/dev/null
for symbol in irq_default_affinity matrix values; do
    if grep -F ".type $symbol, @object" "$work/extern_typedef_array_object.s" >/dev/null || \
       grep -F "$symbol:" "$work/extern_typedef_array_object.s" >/dev/null; then
        echo "FAIL compiler/c0/extern_typedef_array_object: extern symbol $symbol emitted storage" >&2
        exit 1
    fi
done

preprocess invalid_extern_typedef_array_redeclaration
if "$minic" -S "$work/invalid_extern_typedef_array_redeclaration.i" \
    -o "$work/invalid_extern_typedef_array_redeclaration.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    echo "FAIL compiler/c0/invalid_extern_typedef_array_redeclaration: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "conflicting extern object redeclaration" "$work/invalid.stderr" >/dev/null

printf '%s\n' "PASS compiler/c0/extern_typedef_array_object typedef-array=direct linux-cpumask-shape=1 nested-suffix=array-of-array sizeof=48 redeclaration=compatible incompatible=reject storage=none"
