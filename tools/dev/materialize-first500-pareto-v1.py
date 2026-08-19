#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    (ROOT / path).write_text(text)


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


# ---------------------------------------------------------------------------
# 1. Linux first500 cast Pareto: bpfptr_t is a typedef alias of sockptr_t.
#    Generalize the existing lvalue-to-rvalue node to record values so an
#    explicit identity cast keeps the correct C value category without adding
#    aggregate-bitcast semantics or a Linux-specific exception.
# ---------------------------------------------------------------------------

replace_once(
    "src/frontend/parser_expression.c",
    """    if (!minic_type_is_void(target_type) &&
        !minic_type_cast_compatible(target_type, operand->type) &&
        !(minic_type_is_pointer(target_type) && minic_type_is_integer(operand->type) &&
          operand->kind == MINIC_EXPRESSION_INTEGER && operand->value.integer_value == 0)) {
        minic_parser_error(parser, "unsupported cast between these types");
        return false;
    }
""",
    """    if (!minic_type_is_void(target_type) &&
        !minic_type_cast_compatible(target_type, operand->type) &&
        !(minic_type_is_record(target_type) && minic_type_is_record(operand->type) &&
          minic_c0_types_compatible(parser->program, target_type, operand->type)) &&
        !(minic_type_is_pointer(target_type) && minic_type_is_integer(operand->type) &&
          operand->kind == MINIC_EXPRESSION_INTEGER && operand->value.integer_value == 0)) {
        minic_parser_error(parser, "unsupported cast between these types");
        return false;
    }
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """               (minic_type_is_void(expression->type) ||
                minic_type_cast_compatible(expression->type, operand->type) ||
                (minic_type_is_pointer(expression->type) && expression_is_integer_zero(operand)));
""",
    """               (minic_type_is_void(expression->type) ||
                minic_type_cast_compatible(expression->type, operand->type) ||
                (minic_type_is_record(expression->type) && minic_type_is_record(operand->type) &&
                 minic_c0_types_compatible(program, expression->type, operand->type)) ||
                (minic_type_is_pointer(expression->type) && expression_is_integer_zero(operand)));
""",
)

# Normalize an identity record cast to the language's existing lvalue-to-rvalue
# operation. If the operand is already an rvalue, the explicit cast is a no-op
# and the normalized expression ID aliases the operand.
replace_once(
    "src/frontend/cast_normalization.c",
    """static bool append_normalized_cast(MinicC0Program *rewritten,
                                   const MinicExpression *cast_expression,
                                   MinicExpressionId mapped_operand,
                                   MinicExpressionId *normalized_id) {
    const MinicExpression *operand_expression;

    if (rewritten == NULL || cast_expression == NULL || normalized_id == NULL ||
        mapped_operand >= rewritten->expression_count) {
        return false;
    }
    operand_expression = &rewritten->expressions[mapped_operand];

    if (minic_type_is_void(cast_expression->type)) {
""",
    """static bool append_normalized_cast(MinicC0Program *rewritten,
                                   const MinicExpression *cast_expression,
                                   MinicExpressionId mapped_operand,
                                   MinicExpressionId *normalized_id) {
    const MinicExpression *operand_expression;

    if (rewritten == NULL || cast_expression == NULL || normalized_id == NULL ||
        mapped_operand >= rewritten->expression_count) {
        return false;
    }
    operand_expression = &rewritten->expressions[mapped_operand];

    if (minic_type_is_record(cast_expression->type) &&
        minic_type_is_record(operand_expression->type) &&
        cast_expression->type.record_id == operand_expression->type.record_id) {
        MinicType cast_unqualified;
        MinicType operand_unqualified;

        if (!minic_type_unqualified(cast_expression->type, &cast_unqualified) ||
            !minic_type_unqualified(operand_expression->type, &operand_unqualified) ||
            !minic_type_equal(cast_unqualified, operand_unqualified)) {
            return false;
        }
        if (operand_expression->value_category == MINIC_VALUE_RVALUE) {
            *normalized_id = mapped_operand;
            return true;
        }
        if (operand_expression->value_category == MINIC_VALUE_LVALUE) {
            MinicExpression read;

            (void)memset(&read, 0, sizeof(read));
            read.kind = MINIC_EXPRESSION_LVALUE_READ;
            read.span = cast_expression->span;
            read.type = cast_expression->type;
            read.value_category = MINIC_VALUE_RVALUE;
            read.value.unary.operand = mapped_operand;
            return minic_c0_program_add_expression(rewritten, &read, normalized_id);
        }
        return false;
    }

    if (minic_type_is_void(cast_expression->type)) {
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """               minic_type_equal(expression->type, operand->type) &&
               (minic_type_is_integer(expression->type) ||
                minic_type_is_pointer(expression->type) || minic_type_is_double(expression->type));
""",
    """               minic_type_equal(expression->type, operand->type) &&
               (minic_type_is_integer(expression->type) ||
                minic_type_is_pointer(expression->type) || minic_type_is_double(expression->type) ||
                minic_type_is_record(expression->type));
""",
)

# Address-backed record values can now be an explicit lvalue-to-rvalue read.
replace_once(
    "src/frontend/ast.c",
    """        if (expression->value_category != MINIC_VALUE_RVALUE ||
            expression->kind != MINIC_EXPRESSION_STATEMENT) {
            return false;
        }
        result_id = expression->value.statement_expression.result;
        if (result_id == MINIC_EXPRESSION_INVALID || result_id >= expression_id) {
            return false;
        }
        expression_id = result_id;
        remaining -= 1U;
""",
    """        if (expression->value_category != MINIC_VALUE_RVALUE) {
            return false;
        }
        if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
            result_id = expression->value.unary.operand;
        } else if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
            result_id = expression->value.statement_expression.result;
        } else {
            return false;
        }
        if (result_id == MINIC_EXPRESSION_INVALID || result_id >= expression_id) {
            return false;
        }
        expression_id = result_id;
        remaining -= 1U;
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """        return expression->kind == MINIC_EXPRESSION_STATEMENT &&
               minic_riscv64_emit_expression(
                   file, program, function, function_layout, expression_id);
""",
    """        if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
            return minic_riscv64_emit_lvalue_address(file,
                                                     program,
                                                     function,
                                                     function_layout,
                                                     expression->value.unary.operand);
        }
        return expression->kind == MINIC_EXPRESSION_STATEMENT &&
               minic_riscv64_emit_expression(
                   file, program, function, function_layout, expression_id);
""",
)

# ---------------------------------------------------------------------------
# 2. Linux first500 ext4 Pareto: __nonstring__ is a GCC diagnostic attribute on
#    character arrays/pointers. MiniC has no string-warning consumer yet, so
#    recognize it explicitly and preserve its correct no-codegen-effect class.
# ---------------------------------------------------------------------------

replace_once(
    "src/frontend/attribute.h",
    "    MINIC_ATTRIBUTE_WARN_UNUSED_RESULT,\n",
    "    MINIC_ATTRIBUTE_WARN_UNUSED_RESULT,\n    MINIC_ATTRIBUTE_NONSTRING,\n",
)

replace_once(
    "src/frontend/attribute.c",
    """    MINIC_ATTRIBUTE_ENTRY("__warn_unused_result__",
                          MINIC_ATTRIBUTE_WARN_UNUSED_RESULT,
                          MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
""",
    """    MINIC_ATTRIBUTE_ENTRY("__warn_unused_result__",
                          MINIC_ATTRIBUTE_WARN_UNUSED_RESULT,
                          MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    {
        "nonstring",
        sizeof("nonstring") - 1U,
        MINIC_ATTRIBUTE_NONSTRING,
        MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
        MINIC_ATTRIBUTE_TARGET_OBJECT | MINIC_ATTRIBUTE_TARGET_FIELD,
        0U,
        0U,
        true,
    },
    {
        "__nonstring__",
        sizeof("__nonstring__") - 1U,
        MINIC_ATTRIBUTE_NONSTRING,
        MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
        MINIC_ATTRIBUTE_TARGET_OBJECT | MINIC_ATTRIBUTE_TARGET_FIELD,
        0U,
        0U,
        true,
    },
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """    if (descriptor->kind == MINIC_ATTRIBUTE_PACKED) {
        context->is_packed = true;
        return true;
    }
    minic_parser_error(parser, "unsupported GNU record field attribute");
""",
    """    if (descriptor->kind == MINIC_ATTRIBUTE_PACKED) {
        context->is_packed = true;
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_NONSTRING) {
        return true;
    }
    minic_parser_error(parser, "unsupported GNU record field attribute");
""",
)

# Focused fixtures mirror the exact Linux mechanisms without copying Linux source.
write(
    "tests/compiler/c0/identity_record_typedef_cast.c",
    '''typedef struct record_alias_payload {
    long value;
} sockptr_t;

typedef sockptr_t bpfptr_t;

static sockptr_t copy_alias(bpfptr_t source)
{
    return (sockptr_t)source;
}

int main(void)
{
    bpfptr_t source = { .value = 37 };
    return copy_alias(source).value == 37 ? 0 : 1;
}
''',
)

write(
    "tests/compiler/c0/record_field_nonstring_attribute.c",
    '''struct ext4_like_super_block {
    char s_last_mounted[64] __attribute__((__nonstring__));
};

int main(void)
{
    struct ext4_like_super_block value = { .s_last_mounted = "x" };
    return value.s_last_mounted[0] == 'x' ? 0 : 1;
}
''',
)

write(
    "tests/compiler/c0/run-first500-pareto-v1.sh",
    '''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/first500-pareto-v1
mkdir -p "$work"

for name in identity_record_typedef_cast record_field_nonstring_attribute; do
  "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
  "$minic" -S "$work/$name.i" -o "$work/$name.s"
  test -s "$work/$name.s"
done

printf '%s\\n' 'PASS compiler/c0/first500-pareto-v1 identity-record-cast=1 nonstring-field=1'
''',
)

# Keep focused compile coverage in the main C0 gate; runtime differential is picked
# up by run-runtime.sh for the identity record cast.
replace_once(
    "tests/compiler/c0/run.sh",
    """MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-enum-forward-completion.sh"
""",
    """MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-enum-forward-completion.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-first500-pareto-v1.sh"
""",
)

replace_once(
    "tests/compiler/c0/run-runtime.sh",
    "run_case gnu_array_range_runtime 0 gnu_array_range_runtime\n",
    "run_case gnu_array_range_runtime 0 gnu_array_range_runtime\nrun_case identity_record_typedef_cast 0 identity_record_typedef_cast\nrun_case record_field_nonstring_attribute 0 record_field_nonstring_attribute\n",
)

print("FIRST500_PARETO_V1_MATERIALIZED")
