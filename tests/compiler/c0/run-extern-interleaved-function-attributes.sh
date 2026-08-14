#!/bin/sh
set -eu

: "${MINIC:?MINIC must point at the MiniC executable}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

case "$MINIC" in
    /*) ;;
    *) MINIC="$(pwd)/$MINIC" ;;
esac

work="$BUILD_DIR/tests/compiler-c0-extern-interleaved-function-attributes"
mkdir -p "$work"
"$MINIC" -S tests/compiler/c0/extern_interleaved_function_attributes.c -o "$work/probe.s"

grep -q 'warn_slowpath_fmt' "$work/probe.s"
grep -q 'warn_printk' "$work/probe.s"
if grep -q 'extern_declaration_is_function' src/frontend/parser_function.c; then
    echo 'FAIL compiler/c0/extern_interleaved_function_attributes semantic-probe-still-present=1' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/extern_interleaved_function_attributes linux-shape=1 single-pass-extern-head=1 deferred-prefix-attributes=1'
