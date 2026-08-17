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
}

expect_instructions() {
    name=$1
    shift

    for instruction in "$@"; do
        if test "$instruction" = "sw t0, 0(a0)"; then
            grep -E '^  sw t0, 0\((a0|t1)\)$' "$work/$name.s" >/dev/null
        else
            grep -F "  $instruction" "$work/$name.s" >/dev/null
        fi
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
    if ! grep -F "$expected_message" "$work/$name.stderr" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/$name: diagnostic mismatch" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
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
test "$(grep -E -c '^  sw t0, 0\((a0|t1)\)$' "$work/local_assign.s")" -eq 2

compile_source local_reassign locals -DCASE=3
expect_instructions local_reassign \
    "mv s0, sp" "lw a0, 0(s0)" "mulw a0, t0, a0" \
    "addi a0, s0, 0" "sw t0, 0(a0)"
test "$(grep -E -c '^  sw t0, 0\((a0|t1)\)$' "$work/local_reassign.s")" -eq 2

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

compile_source typedef_multidimensional_array typedef_multidimensional_array
expect_instructions typedef_multidimensional_array \
    "mv s0, sp" "li a0, 0" "j .Lmain_return"

compile_source static_global_array static_global_array
grep -F ".section .rodata" "$work/static_global_array.s" >/dev/null
grep -F ".type table, @object" "$work/static_global_array.s" >/dev/null
grep -F ".align 2" "$work/static_global_array.s" >/dev/null
grep -F "table:" "$work/static_global_array.s" >/dev/null
grep -F "  .word 99" "$work/static_global_array.s" >/dev/null
grep -F "  .word 124" "$work/static_global_array.s" >/dev/null
grep -F "  .word 1" "$work/static_global_array.s" >/dev/null
grep -F "  .word 0" "$work/static_global_array.s" >/dev/null
grep -F ".size table, 16" "$work/static_global_array.s" >/dev/null
if grep -F ".globl table" "$work/static_global_array.s" >/dev/null; then
    printf '%s\n' "FAIL compiler/c0/static_global_array: internal object exported" >&2
    exit 1
fi
printf '%s\n' "PASS compiler/c0/static_global_array"

expect_compile_failure \
    invalid_duplicate_block_local \
    "duplicate local declaration"
expect_compile_failure \
    invalid_out_of_scope_local \
    "use of undeclared local"
expect_compile_failure \
    invalid_address_of_rvalue \
    "address-of requires an lvalue object or function designator"
expect_compile_failure \
    invalid_dereference_integer \
    "dereference requires a pointer operand"
expect_compile_failure \
    invalid_pointer_initializer \
    "initializer type does not match local type"
expect_compile_failure \
    invalid_assignment_rvalue \
    "assignment expression requires a modifiable object lvalue"
expect_compile_failure \
    invalid_pointer_assignment_type \
    "assignment expression type does not match target type"
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
    "return expression does not match function return type"
expect_compile_failure \
    invalid_array_index_type \
    "array index must have integer type"
expect_compile_failure \
    invalid_duplicate_record_field \
    "duplicate record field"
expect_compile_failure \
    invalid_duplicate_record_definition \
    "duplicate record definition"
MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-gnu-empty-records.sh"
expect_compile_failure \
    invalid_conflicting_const_parameter \
    "conflicting function declaration"
expect_compile_failure \
    invalid_named_void_parameter \
    "parameter type cannot be bare void"
expect_compile_failure \
    invalid_duplicate_typedef \
    "duplicate typedef name"
expect_compile_failure \
    invalid_void_typedef \
    "typedef cannot name bare void"
expect_compile_failure \
    invalid_too_many_global_initializers \
    "too many nested static array initializers"
compile_source static_scalar_global static_scalar_global
expect_instructions static_scalar_global "la a0, value" "lw a0, 0(a0)"
grep -F "  .word 7" "$work/static_scalar_global.s" >/dev/null

expect_compile_failure \
    invalid_braced_scalar_static_global \
    "expected integer constant expression"
expect_compile_failure invalid_return "expected expression"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-gnu-void-return-expression.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-gnu-omitted-conditional.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-gnu-static-local-interleaved-attribute.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-runtime-local-array-initializer.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-array-parameter-adjustment.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-function-parameter-adjustment.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-function-pointer-qualifiers.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-gnu-function-pointer-bridge-call.sh"

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-gnu-function-copy-alias.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-pointer-to-bool-conversion.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-block-scope-extern-multi-declarator.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-record-rvalue-member.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-record-conditional-materialization.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-gnu-packed-record-field.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-comma-operator.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-comma-operator.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-block-scope-extern-object.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-transparent-union.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-gnu-zero-length-array.sh"

MINIC="$minic" BUILD_DIR="$work/gnu-extern-void-symbol" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-gnu-extern-void-symbol.sh"

MINIC="$minic" BUILD_DIR="$work/extern-typedef-array-object" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-extern-typedef-array-object.sh"

MINIC="$minic" BUILD_DIR="$work/expression-statement-entry" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-expression-statement-entry.sh"

MINIC="$minic" BUILD_DIR="$work/function-typed-declarator" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-function-typed-declarator.sh"

MINIC="$minic" BUILD_DIR="$work/gnu-weak-function-symbol" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-gnu-weak-function-symbol.sh"

MINIC="$minic" BUILD_DIR="$work/pragma-pack-record-layout" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-pragma-pack-record-layout.sh"

MINIC="$minic" BUILD_DIR="$work/conditional-null-pointer-constant" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-conditional-null-pointer-constant.sh"

MINIC="$minic" BUILD_DIR="$work/deferred-declarator-attributes" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-deferred-declarator-attributes.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-static-array-designators.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-external-inferred-record-array.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-builtin-unary-family.sh"
