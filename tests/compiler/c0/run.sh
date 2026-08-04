#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0

mkdir -p "$work"

compile_source() {
    name=$1
    source_name=$2
    shift 2

    "$host_cc" -E -P -x c "$@" \
        "$root/tests/compiler/c0/$source_name.c" -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
    grep -F ".globl main" "$work/$name.s" >/dev/null
    grep -F ".Lmain_return:" "$work/$name.s" >/dev/null
}

expect_instructions() {
    name=$1
    shift

    for instruction in "$@"; do
        grep -F "  $instruction" "$work/$name.s" >/dev/null
    done
    printf '%s\n' "PASS compiler/c0/$name"
}

compile_source empty_main empty_main
expect_instructions empty_main "li a0, 0" "j .Lmain_return"

compile_source return_0 return_0
expect_instructions return_0 "li a0, 0" "j .Lmain_return"

compile_source return_42 return_42
expect_instructions return_42 "li a0, 42" "j .Lmain_return"

compile_source arithmetic_precedence arithmetic -DCASE=1
expect_instructions arithmetic_precedence \
    "mulw a0, t0, a0" "addw a0, t0, a0"

compile_source arithmetic_parentheses arithmetic -DCASE=2
expect_instructions arithmetic_parentheses \
    "subw a0, t0, a0" "mulw a0, t0, a0"

compile_source arithmetic_divrem arithmetic -DCASE=3
expect_instructions arithmetic_divrem \
    "divw a0, t0, a0" "remw a0, t0, a0" "addw a0, t0, a0"

compile_source arithmetic_unary arithmetic -DCASE=4
expect_instructions arithmetic_unary \
    "negw a0, a0" "addw a0, t0, a0"

compile_source local_init locals -DCASE=1
expect_instructions local_init \
    "mv t1, sp" "sw a0, 0(t1)" "lw a0, 0(t1)"

compile_source local_assign locals -DCASE=2
expect_instructions local_assign \
    "mv t1, sp" "sw a0, 0(t1)" "sw a0, 4(t1)" \
    "lw a0, 0(t1)" "lw a0, 4(t1)" "addw a0, t0, a0"

compile_source local_reassign locals -DCASE=3
expect_instructions local_reassign \
    "mv t1, sp" "lw a0, 0(t1)" "mulw a0, t0, a0" "sw a0, 0(t1)"

compile_source comparison_equal comparisons -DCASE=1
expect_instructions comparison_equal "xor a0, t0, a0" "seqz a0, a0"

compile_source comparison_not_equal comparisons -DCASE=2
expect_instructions comparison_not_equal "xor a0, t0, a0" "snez a0, a0"

compile_source comparison_less comparisons -DCASE=3
expect_instructions comparison_less "slt a0, t0, a0"

compile_source comparison_less_equal comparisons -DCASE=4
expect_instructions comparison_less_equal \
    "slt a0, a0, t0" "xori a0, a0, 1"

compile_source comparison_greater comparisons -DCASE=5
expect_instructions comparison_greater "slt a0, a0, t0"

compile_source comparison_greater_equal comparisons -DCASE=6
expect_instructions comparison_greater_equal \
    "slt a0, t0, a0" "xori a0, a0, 1"

compile_source comparison_precedence comparisons -DCASE=7
expect_instructions comparison_precedence \
    "mulw a0, t0, a0" "addw a0, t0, a0" \
    "xor a0, t0, a0" "seqz a0, a0"

compile_source comparison_local comparisons -DCASE=8
expect_instructions comparison_local \
    "sw a0, 0(t1)" "lw a0, 0(t1)" "slt a0, t0, a0"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_return.c" -o "$work/invalid_return.i"
if "$minic" -S "$work/invalid_return.i" -o "$work/invalid_return.s" \
    >"$work/invalid_return.stdout" 2>"$work/invalid_return.stderr"; then
    printf '%s\n' "FAIL compiler/c0/invalid_return: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "expected expression" "$work/invalid_return.stderr" >/dev/null
printf '%s\n' "PASS compiler/c0/invalid_return"
