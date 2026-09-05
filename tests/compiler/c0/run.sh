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
    # Core owns the production function route. Keep this contract on route
    # identity and CFG shape, not on frame/register allocation policy.
    grep -F ".globl main" "$work/$name.s" >/dev/null
    grep -F ".Lmain_core_bb" "$work/$name.s" >/dev/null
    grep -F ".Lmain_core_return:" "$work/$name.s" >/dev/null
    grep -E '^[[:space:]]+ret$' "$work/$name.s" >/dev/null
}

expect_instructions() {
    name=$1
    shift

    for instruction in "$@"; do
        case "$instruction" in
            "mv s0, sp"|"sd a0, 0(sp)"|"ld t0, 0(sp)"|"addi a0, s0, "*)
                # Legacy frame/register-placement details are not part of the
                # Core semantic contract. Load/store/value-flow checks below
                # still prove that the source operation is emitted.
                ;;
            "j .Lmain_return")
                # Core return edges use a far PC-relative transfer so bootstrap
                # functions are not limited by JAL's signed 21-bit displacement.
                if ! grep -F "  j .Lmain_core_return" "$work/$name.s" >/dev/null; then
                    grep -F "  lla t6, .Lmain_core_return" "$work/$name.s" >/dev/null
                    grep -F "  jalr zero, t6, 0" "$work/$name.s" >/dev/null
                fi
                ;;
            "j .Lif_"*)
                if ! grep -E '^[[:space:]]+j[[:space:]]+\.Lmain_core_bb[0-9]+$' "$work/$name.s" >/dev/null; then
                    grep -E '^[[:space:]]+lla[[:space:]]+t6,[[:space:]]*\.Lmain_core_bb[0-9]+$' "$work/$name.s" >/dev/null
                    grep -F "  jalr zero, t6, 0" "$work/$name.s" >/dev/null
                fi
                ;;
            "beqz a0, .Lif_"*)
                # Current Core conditional terminators keep only a local beqz
                # and transfer both successors through the far-jump sequence.
                if ! grep -E '^[[:space:]]+bnez[[:space:]]+[^,]+,[[:space:]]*\.Lmain_core_bb[0-9]+$' "$work/$name.s" >/dev/null; then
                    grep -E '^[[:space:]]+beqz[[:space:]]+t0,[[:space:]]*1f$' "$work/$name.s" >/dev/null
                    test "$(grep -E -c '^[[:space:]]+lla[[:space:]]+t6,[[:space:]]*\.Lmain_core_bb[0-9]+$' "$work/$name.s")" -ge 2
                fi
                ;;
            "li a0, "*)
                immediate=${instruction#"li a0, "}
                grep -E "^[[:space:]]+li[[:space:]]+[^,]+,[[:space:]]*$immediate$" \
                    "$work/$name.s" >/dev/null
                ;;
            "la a0, "*)
                symbol=${instruction#"la a0, "}
                grep -E "^[[:space:]]+la[[:space:]]+[^,]+,[[:space:]]*$symbol$" \
                    "$work/$name.s" >/dev/null
                ;;
            "xori a0, a0, 1")
                # Legacy <=/>= inverted the less-than bit with xori 1.
                # Core models that inversion explicitly as SCALAR_IS_ZERO,
                # whose RV64 lowering is seqz.
                grep -E '^[[:space:]]+seqz[[:space:]]+' "$work/$name.s" >/dev/null
                ;;
            "xori a0, a0, "*)
                immediate=${instruction#"xori a0, a0, "}
                grep -E "^[[:space:]]+xori[[:space:]]+[^,]+,[[:space:]]*[^,]+,[[:space:]]*$immediate$" \
                    "$work/$name.s" >/dev/null
                ;;
            addw\ *|subw\ *|mulw\ *|divw\ *|remw\ *|negw\ *)
                opcode=${instruction%% *}
                opcode=${opcode%w}
                grep -E "^[[:space:]]+$opcode[[:space:]]+" "$work/$name.s" >/dev/null
                ;;
            snez\ *)
                # Core has SCALAR_EQUAL + SCALAR_IS_ZERO rather than a target-
                # shaped not-equal instruction. Canonical a != b is xor +
                # seqz + seqz. Accept direct snez too, but require two zero
                # tests for the Core-normalized shape so equality cannot pass.
                if ! grep -E '^[[:space:]]+snez[[:space:]]+' "$work/$name.s" >/dev/null; then
                    test "$(grep -E -c '^[[:space:]]+seqz[[:space:]]+' "$work/$name.s")" -ge 2
                fi
                ;;
            xor\ *|seqz\ *|slt\ *|lw\ *|sw\ *)
                opcode=${instruction%% *}
                grep -E "^[[:space:]]+$opcode[[:space:]]+" "$work/$name.s" >/dev/null
                ;;
            *)
                printf '%s\n' \
                    "FAIL compiler/c0/$name: unmigrated legacy instruction contract: $instruction" >&2
                exit 1
                ;;
        esac
    done
    printf '%s\n' "PASS compiler/c0/$name normalized=core-contract"
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
compile_source block_scope_gcc_diagnostic_pragma block_scope_gcc_diagnostic_pragma
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
test "$(grep -E -c '^[[:space:]]+sw[[:space:]]+' "$work/local_assign.s")" -ge 2

compile_source local_reassign locals -DCASE=3
expect_instructions local_reassign \
    "mv s0, sp" "lw a0, 0(s0)" "mulw a0, t0, a0" \
    "addi a0, s0, 0" "sw t0, 0(a0)"
test "$(grep -E -c '^[[:space:]]+sw[[:space:]]+' "$work/local_reassign.s")" -ge 2

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
test "$(grep -E -c '^[[:space:]]+seqz[[:space:]]+' "$work/logical_not_recursive.s")" -eq 2

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
if ! test "$(grep -E -c '^[[:space:]]+bnez[[:space:]]+[^,]+,[[:space:]]*\.Lmain_core_bb[0-9]+$' "$work/if_nested_dangling_else.s")" -eq 2; then
    test "$(grep -E -c '^[[:space:]]+beqz[[:space:]]+t0,[[:space:]]*1f$' "$work/if_nested_dangling_else.s")" -eq 2
fi

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

compile_source c11_noreturn c11_noreturn
expect_instructions c11_noreturn "li a0, 0" "j .Lmain_return"

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
compile_source void_typedef void_typedef
printf '%s\n' "PASS compiler/c0/void_typedef"

expect_compile_failure \
    invalid_void_typedef \
    "local object cannot have void type"
expect_compile_failure \
    invalid_too_many_global_initializers \
    "too many nested static array initializers"
compile_source static_scalar_global static_scalar_global
expect_instructions static_scalar_global "la a0, value" "lw a0, 0(a0)"
grep -F "  .word 7" "$work/static_scalar_global.s" >/dev/null

compile_source braced_scalar_static_global invalid_braced_scalar_static_global
grep -F "  .word 1" "$work/braced_scalar_static_global.s" >/dev/null
printf '%s\n' "PASS compiler/c0/braced_scalar_static_global"
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
sh "$root/tests/compiler/c0/run-character-array-string-initializer.sh"

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
sh "$root/tests/compiler/c0/run-function-pointer-field-array.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-gnu-function-pointer-bridge-call.sh"

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-gnu-function-copy-alias.sh"

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-linux-tail-batch3.sh"

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-linux-tail-batch4.sh"

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

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-gnu-switch-case-range.sh"

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-string-octal-escape.sh"

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-gnu-signed-left-shift-ice.sh"

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-static-local-self-reference.sh"

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-record-value-initialize.sh"

MINIC="$minic" HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-braced-static-scalar.sh"

MINIC="$minic" HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-static-pointer-constant-conditional.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-runtime-record-zero-shorthand.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-external-array-redeclaration-owner.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-static-local-prefix-attributes.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-gnu-object-unused-alias.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-alignof-expression.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-static-record-array-suffix-attributes.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-static-record-single-char-array.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-preformed-static-array.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-external-tentative-declarator-list.sh"

MINIC="$minic" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-fixed-call-character-pointer.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-small-record-copy-return.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-backward-static-aggregate-overlay.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-gnu-weak-external-objects.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-static-record-nested-designator-string-overlay.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-static-integer-address-relocation.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-static-union-zero-overlay.sh"


MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-enum-forward-completion.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-first500-pareto-v1.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-first500-static-array-pointer-v1.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-enum-mode-byte.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-declaration-head-variadic.sh"

MINIC="$minic" \
RISCV_CC="${RISCV_CC:-riscv64-linux-gnu-gcc}" \
QEMU_RISCV64="${QEMU_RISCV64:-qemu-riscv64}" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-builtin-va-copy.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-static-storage-scalar-array-owner.sh"


MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-static-record-array-member-designator.sh"

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-linux-tail-batch5.sh"

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-linux-tail-batch6.sh"

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-linux-tail-batch7.sh"

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-scalar-compound-literal.sh"


MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-linux-tail-batch8.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-gnu-local-object-alignment.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-builtin-memset-call.sh"

MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-float-to-double-fixed-call.sh"
MINIC="$minic" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-arithmetic-fixed-call-dead-long-double.sh"
