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
    grep -F "  sd ra, " "$work/$name.s" >/dev/null
    grep -F "  sd s0, " "$work/$name.s" >/dev/null
    grep -F "  mv s0, sp" "$work/$name.s" >/dev/null
    grep -F "  ld ra, " "$work/$name.s" >/dev/null
    grep -F "  ld s0, " "$work/$name.s" >/dev/null
    if grep -F "(t1)" "$work/$name.s" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/$name: caller-saved t1 used as local base" >&2
        exit 1
    fi
}

expect_instructions() {
    name=$1
    shift

    for instruction in "$@"; do
        grep -F "  $instruction" "$work/$name.s" >/dev/null
    done
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_compile_failure() {
    name=$1
    expected_message=$2

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' \
            "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$expected_message" "$work/$name.stderr" >/dev/null
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
    "mv s0, sp" "sd a0, 0(sp)" "addi a0, s0, 0" \
    "ld t0, 0(sp)" "sw t0, 0(a0)" "lw a0, 0(s0)"

compile_source local_assign locals -DCASE=2
expect_instructions local_assign \
    "mv s0, sp" "addi a0, s0, 0" "addi a0, s0, 4" \
    "sw t0, 0(a0)" "lw a0, 0(s0)" "lw a0, 4(s0)" \
    "addw a0, t0, a0"
test "$(grep -c -F '  sw t0, 0(a0)' "$work/local_assign.s")" -eq 2

compile_source local_reassign locals -DCASE=3
expect_instructions local_reassign \
    "mv s0, sp" "lw a0, 0(s0)" "mulw a0, t0, a0" \
    "addi a0, s0, 0" "sw t0, 0(a0)"
test "$(grep -c -F '  sw t0, 0(a0)' "$work/local_reassign.s")" -eq 2

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
    "addi a0, s0, 0" "sw t0, 0(a0)" \
    "lw a0, 0(s0)" "slt a0, t0, a0"

compile_source logical_not_zero logical_not -DCASE=1
expect_instructions logical_not_zero "seqz a0, a0"

compile_source logical_not_nonzero logical_not -DCASE=2
expect_instructions logical_not_nonzero "seqz a0, a0"

compile_source logical_not_recursive logical_not -DCASE=3
expect_instructions logical_not_recursive "seqz a0, a0"
test "$(grep -c -F '  seqz a0, a0' "$work/logical_not_recursive.s")" -eq 2

compile_source logical_not_comparison logical_not -DCASE=4
expect_instructions logical_not_comparison \
    "slt a0, t0, a0" "seqz a0, a0"

compile_source logical_not_local logical_not -DCASE=5
expect_instructions logical_not_local \
    "lw a0, 0(s0)" "seqz a0, a0"

compile_source if_true_assignment if_else -DCASE=1
expect_instructions if_true_assignment \
    "beqz a0, .Lif_else_0" "sw t0, 0(a0)" \
    "j .Lif_end_0"

compile_source if_false_else if_else -DCASE=2
expect_instructions if_false_else \
    "beqz a0, .Lif_else_0" "j .Lif_end_0"

compile_source if_comparison_block if_else -DCASE=3
expect_instructions if_comparison_block \
    "slt a0, t0, a0" "beqz a0, .Lif_else_0" \
    "addw a0, t0, a0"

compile_source if_nested_dangling_else if_else -DCASE=4
expect_instructions if_nested_dangling_else \
    "beqz a0, .Lif_else_0" "beqz a0, .Lif_else_1"
test "$(grep -c -F '  beqz a0, .Lif_else_' "$work/if_nested_dangling_else.s")" -eq 2

compile_source if_branch_return if_else -DCASE=5
expect_instructions if_branch_return \
    "beqz a0, .Lif_else_0" "j .Lmain_return"

compile_source if_false_fallthrough if_else -DCASE=6
expect_instructions if_false_fallthrough \
    "beqz a0, .Lif_else_0" "lw a0, 0(s0)"

compile_source if_multi_statement if_else -DCASE=7
expect_instructions if_multi_statement \
    "beqz a0, .Lif_else_0" "addw a0, t0, a0" \
    "mulw a0, t0, a0"

compile_source array_declaration array_declaration
expect_instructions array_declaration "mv s0, sp" "li a0, 0" "j .Lmain_return"

compile_source record_definition record_definition
expect_instructions record_definition "mv s0, sp" "li a0, 0" "j .Lmain_return"

compile_source function_typed_prototype function_typed_prototype
expect_instructions function_typed_prototype \
    "mv s0, sp" "li a0, 0" "j .Lmain_return"

expect_compile_failure \
    invalid_duplicate_block_local \
    "duplicate local declaration"
expect_compile_failure \
    invalid_out_of_scope_local \
    "use of undeclared local"
expect_compile_failure \
    invalid_address_of_rvalue \
    "address-of requires an lvalue operand"
expect_compile_failure \
    invalid_dereference_integer \
    "dereference requires a pointer operand"
expect_compile_failure \
    invalid_pointer_initializer \
    "initializer type does not match local type"
expect_compile_failure \
    invalid_assignment_rvalue \
    "assignment target must be an lvalue"
expect_compile_failure \
    invalid_pointer_assignment_type \
    "assignment type does not match target type"
expect_compile_failure \
    invalid_pointer_add_pointer \
    "unsupported pointer arithmetic operands"
expect_compile_failure \
    invalid_integer_subtract_pointer \
    "unsupported pointer arithmetic operands"
expect_compile_failure \
    invalid_zero_length_array \
    "array bound must be greater than zero"
expect_compile_failure \
    invalid_array_initializer \
    "array initializers are not supported yet"
expect_compile_failure \
    invalid_bare_array_use \
    "array object requires a subscript"
expect_compile_failure \
    invalid_array_index_type \
    "array index must have int type"
expect_compile_failure \
    invalid_duplicate_record_field \
    "duplicate record field"
expect_compile_failure \
    invalid_duplicate_record_definition \
    "duplicate record definition"
expect_compile_failure \
    invalid_empty_record \
    "record definition requires at least one field"
expect_compile_failure \
    invalid_conflicting_const_parameter \
    "conflicting function declaration"
expect_compile_failure \
    invalid_named_void_parameter \
    "parameter type cannot be bare void"
expect_compile_failure invalid_return "expected expression"
