#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug/function-typed-declarator"}
mkdir -p "$work"

preprocess() {
    name=$1
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
}

preprocess function_typed_declarator
"$minic" -S "$work/function_typed_declarator.i" -o "$work/function_typed_declarator.s"
grep -F "  call __SCT__perf_snapshot_branch_stack" "$work/function_typed_declarator.s" >/dev/null
grep -F "  call typed_direct" "$work/function_typed_declarator.s" >/dev/null
grep -F "  call parenthesized_direct" "$work/function_typed_declarator.s" >/dev/null
grep -F "  la a0, callback_slot" "$work/function_typed_declarator.s" >/dev/null
grep -F "  jalr ra, t0, 0" "$work/function_typed_declarator.s" >/dev/null

preprocess invalid_function_typed_redeclaration
if "$minic" -S "$work/invalid_function_typed_redeclaration.i" \
    -o "$work/invalid_function_typed_redeclaration.s" \
    >"$work/invalid-redecl.stdout" 2>"$work/invalid-redecl.stderr"; then
    echo "FAIL compiler/c0/invalid_function_typed_redeclaration: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "conflicting function declaration" "$work/invalid-redecl.stderr" >/dev/null

preprocess invalid_function_typed_definition
if "$minic" -S "$work/invalid_function_typed_definition.i" \
    -o "$work/invalid_function_typed_definition.s" \
    >"$work/invalid-definition.stdout" 2>"$work/invalid-definition.stderr"; then
    echo "FAIL compiler/c0/invalid_function_typed_definition: compilation unexpectedly succeeded" >&2
    exit 1
fi

printf '%s\n' "PASS compiler/c0/function_typed_declarator entity=function typedef=direct typeof=function parenthesized=1 redeclaration=shared-signature pointer-typedef=object+jalr definition=reject"
