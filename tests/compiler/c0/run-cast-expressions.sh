#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-cast-expressions"

mkdir -p "$work"

compile_success() {
    name=$1

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure() {
    name=$1
    message=$2

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' \
            "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$message" "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

compile_success cast_expressions
grep -F '.Lmain_core_bb' "$work/cast_expressions.s" >/dev/null
grep -F '.Lmain_core_return:' "$work/cast_expressions.s" >/dev/null
printf '%s\n' "PASS compiler/c0/cast_integer_lowering normalized=core-conversion"

compile_success cast_integer_conversion
grep -F '.Lmain_core_bb' "$work/cast_integer_conversion.s" >/dev/null
grep -F '.Lmain_core_return:' "$work/cast_integer_conversion.s" >/dev/null
printf '%s\n' "PASS compiler/c0/cast_integer_conversion normalized=core-conversion"

compile_success cast_typedef_shadow
compile_success cast_plain_char
compile_success cast_integer_to_double
grep -E '^[[:space:]]+fcvt\.d\.w[[:space:]]+[^,]+,[[:space:]]*[^,]+$' "$work/cast_integer_to_double.s" >/dev/null
printf '%s\n' "PASS compiler/c0/cast_integer_to_double_lowering"

compile_success cast_double_to_integer
grep -E '^[[:space:]]+fcvt\.w\.d[[:space:]]+[^,]+,[[:space:]]*[^,]+,[[:space:]]*rtz$' "$work/cast_double_to_integer.s" >/dev/null
printf '%s\n' "PASS compiler/c0/cast_double_to_integer_lowering"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/null_pointer_constant.c" \
    -o "$work/null_pointer_constant.i"
"$minic" -S "$work/null_pointer_constant.i" -o "$work/null_pointer_constant.s"
grep -E '^[[:space:]]+li[[:space:]]+[^,]+,[[:space:]]*0$' "$work/null_pointer_constant.s" >/dev/null
grep -F ".Lmake_null_core_return:" "$work/null_pointer_constant.s" >/dev/null
printf '%s\n' "PASS compiler/c0/null_pointer_constant"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="$build_dir" \
sh "$root/tests/compiler/c0/run-pointer-integer-casts.sh"

compile_success cast_integer_to_float
grep -F '  fcvt.d.w ' "$work/cast_integer_to_float.s" >/dev/null
grep -F '  fcvt.s.d ' "$work/cast_integer_to_float.s" >/dev/null
grep -F '  li t0, 0x7f800000' "$work/cast_integer_to_float.s" >/dev/null
grep -F '  li t0, 0x3e800000' "$work/cast_integer_to_float.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/cast_integer_to_float lowering=int->double->float literals=binary32 overflow=inf'
expect_failure \
    invalid_cast_assignment_target \
    "assignment expression requires a modifiable object lvalue"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="$build_dir" \
sh "$root/tests/compiler/c0/run-pointer-qualifications.sh"
