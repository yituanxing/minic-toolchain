#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-predefined-func-name"
mkdir -p "$work"

"$minic" -S "$root/tests/programs/c0/predefined_func_name.c" -o "$work/positive.s"
object_count=$(grep -c '^\.Lminic_func_name_[0-9][0-9]*:$' "$work/positive.s")
if [ "$object_count" -ne 1 ]; then
    printf '%s\n' "FAIL compiler/c0/predefined-func-name: expected one stable backing object, got $object_count" >&2
    exit 1
fi
# check_func is 10 characters; the backing array includes its terminating NUL.
grep -F '.size .Lminic_func_name_' "$work/positive.s" | grep -F ', 11' >/dev/null

expect_failure() {
    name=$1
    message=$2
    if "$minic" -S "$root/tests/compiler/c0/$name.c" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    if ! grep -F "$message" "$work/$name.stderr" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/$name: diagnostic mismatch" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure invalid_predefined_func_name_write \
    'assignment expression requires a modifiable object lvalue'
expect_failure invalid_predefined_func_name_file_scope \
    '__func__ is only available inside a function'

printf '%s\n' \
    'PASS compiler/c0/predefined-func-name object=stable type=static-const-char-array sizeof=array decay=shared lexical-name=1'
