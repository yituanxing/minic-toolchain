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


# Semantic AST: va_start/va_end are language-level builtin effects, not fake calls
# to a target helper symbol. Both own one va_list lvalue child.
replace_once(
    "src/frontend/ast.h",
    """    MINIC_EXPRESSION_STATEMENT,
    MINIC_EXPRESSION_BUILTIN_UNARY,
    MINIC_EXPRESSION_BUILTIN_OVERFLOW
""",
    """    MINIC_EXPRESSION_STATEMENT,
    MINIC_EXPRESSION_BUILTIN_VA_START,
    MINIC_EXPRESSION_BUILTIN_VA_END,
    MINIC_EXPRESSION_BUILTIN_UNARY,
    MINIC_EXPRESSION_BUILTIN_OVERFLOW
""",
)

replace_once(
    "src/frontend/ast_traversal.c",
    """    case MINIC_EXPRESSION_LVALUE_READ:
    case MINIC_EXPRESSION_UNARY:
        return visit_expression_id(&expression->value.unary.operand, visitor, context);
""",
    """    case MINIC_EXPRESSION_LVALUE_READ:
    case MINIC_EXPRESSION_UNARY:
    case MINIC_EXPRESSION_BUILTIN_VA_START:
    case MINIC_EXPRESSION_BUILTIN_VA_END:
        return visit_expression_id(&expression->value.unary.operand, visitor, context);
""",
)

# Parser: validate GNU builtin contracts at the language boundary. The last named
# argument is a compile-time designator, not a runtime child; parameters are the
# first locals in function order, so require the exact final named parameter.
parser_helpers = r'''static bool parse_builtin_va_list_target(MinicParser *parser,
                                         MinicExpressionId *target_id,
                                         MinicSourceSpan *span) {
    const MinicExpression *target;

    if (parser == NULL || target_id == NULL || span == NULL) {
        return false;
    }
    *span = parser->current.span;
    if (!parse_expression_internal(parser, target_id, 0U, false)) {
        return false;
    }
    target = minic_c0_program_expression(parser->program, *target_id);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(target->type) || minic_type_is_const(target->type)) {
        minic_parser_error(parser, "GNU va builtin requires a modifiable va_list lvalue");
        return false;
    }
    return true;
}

static bool parse_builtin_va_start(MinicParser *parser, MinicExpressionId *expression_id) {
    const MinicFunction *function;
    MinicExpression expression;
    MinicExpressionId target_id;
    MinicLocalId last_parameter_id;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicSourceSpan target_span;

    function = parser != NULL
                   ? minic_c0_program_function(parser->program, parser->current_function)
                   : NULL;
    if (parser == NULL || expression_id == NULL || function == NULL || !function->is_variadic ||
        function->parameter_count == 0U) {
        if (parser != NULL) {
            minic_parser_error(parser, "__builtin_va_start requires a variadic function");
        }
        return false;
    }

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_va_start") ||
        !parse_builtin_va_list_target(parser, &target_id, &target_span) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_va_start")) {
        return false;
    }
    (void)target_span;

    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "__builtin_va_start requires the last named parameter");
        return false;
    }
    last_parameter_id = minic_parser_find_local(parser, parser->current.span);
    if (last_parameter_id == MINIC_LOCAL_INVALID ||
        last_parameter_id != parser->local_begin + function->parameter_count - 1U) {
        minic_parser_error(parser, "__builtin_va_start second argument must be the last named parameter");
        return false;
    }
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_va_start");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_VA_START;
    expression.span.begin = begin;
    expression.span.end = end;
    expression.type = minic_type_void();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = target_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_builtin_va_end(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicExpressionId target_id;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicSourceSpan target_span;

    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_va_end") ||
        !parse_builtin_va_list_target(parser, &target_id, &target_span)) {
        return false;
    }
    (void)target_span;
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_va_end");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_VA_END;
    expression.span.begin = begin;
    expression.span.end = end;
    expression.type = minic_type_void();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = target_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

'''
replace_once(
    "src/frontend/parser_expression.c",
    "static bool parse_primary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {\n",
    parser_helpers +
    "static bool parse_primary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {\n",
)

replace_once(
    "src/frontend/parser_expression.c",
    """    if (generic_token_text_equals(parser, "__builtin_unreachable")) {
""",
    """    if (generic_token_text_equals(parser, "__builtin_va_start")) {
        if (!parse_builtin_va_start(parser, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_va_end")) {
        if (!parse_builtin_va_end(parser, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_unreachable")) {
""",
)

# Verifier owns structural/type legality; variadic-function and last-parameter
# constraints were already checked by parser, because expression verification is
# intentionally function-owner agnostic.
replace_once(
    "src/frontend/ast_verifier.c",
    """    case MINIC_EXPRESSION_BUILTIN_UNARY: {
""",
    """    case MINIC_EXPRESSION_BUILTIN_VA_START:
    case MINIC_EXPRESSION_BUILTIN_VA_END:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return operand != NULL && operand->value_category == MINIC_VALUE_LVALUE &&
               minic_type_is_pointer(operand->type) && !minic_type_is_const(operand->type) &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_is_void(expression->type);
    case MINIC_EXPRESSION_BUILTIN_UNARY: {
""",
)

# RV64: factor the existing internal va-start pointer computation into one target
# owner, then lower the semantic builtin by storing that pointer into the va_list.
va_helper = r'''static bool minic_riscv64_emit_va_start_pointer(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout) {
    MinicRiscv64FrameLayout frame_layout;

    if (file == NULL || program == NULL || function == NULL || function_layout == NULL ||
        !function->is_variadic ||
        !minic_riscv64_frame_layout_from_function_layout(
            program, function, function_layout, &frame_layout)) {
        return false;
    }
    if (frame_layout.varargs_offset <= 2047U) {
        return fprintf(file, "  addi a0, s0, %zu\n", frame_layout.varargs_offset) >= 0;
    }
    return fprintf(file,
                   "  li t0, %zu\n"
                   "  add a0, s0, t0\n",
                   frame_layout.varargs_offset) >= 0;
}

'''
replace_once(
    "src/target/riscv64/codegen_expression.c",
    "static bool type_is_condition_scalar(MinicType type) {\n",
    va_helper + "static bool type_is_condition_scalar(MinicType type) {\n",
)

old_internal = r'''        if (!is_indirect && direct_callee != NULL && direct_callee->name_length == 16U &&
            strcmp(direct_callee->name, "__minic_va_start") == 0) {
            MinicRiscv64FrameLayout frame_layout;

            if (argument_count != 0U || !function->is_variadic ||
                !minic_type_is_pointer(expression->type) ||
                !minic_riscv64_frame_layout_from_function_layout(
                    program, function, function_layout, &frame_layout)) {
                return false;
            }
            if (frame_layout.varargs_offset <= 2047U) {
                return fprintf(file, "  addi a0, s0, %zu\n", frame_layout.varargs_offset) >= 0;
            }
            return fprintf(file,
                           "  li t0, %zu\n"
                           "  add a0, s0, t0\n",
                           frame_layout.varargs_offset) >= 0;
        }
'''
new_internal = r'''        if (!is_indirect && direct_callee != NULL && direct_callee->name_length == 16U &&
            strcmp(direct_callee->name, "__minic_va_start") == 0) {
            return argument_count == 0U && minic_type_is_pointer(expression->type) &&
                   minic_riscv64_emit_va_start_pointer(
                       file, program, function, function_layout);
        }
'''
replace_once("src/target/riscv64/codegen_expression.c", old_internal, new_internal)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    case MINIC_EXPRESSION_BUILTIN_UNREACHABLE:
""",
    r'''    case MINIC_EXPRESSION_BUILTIN_VA_START: {
        const MinicExpression *target;
        MinicExpressionId target_id;

        target_id = expression->value.unary.operand;
        target = minic_c0_program_expression(program, target_id);
        if (target == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_pointer(target->type) || minic_type_is_const(target->type) ||
            !minic_riscv64_emit_lvalue_address(
                file, program, function, function_layout, target_id) ||
            fprintf(file, "  mv t4, a0\n") < 0 ||
            !minic_riscv64_emit_va_start_pointer(file, program, function, function_layout)) {
            return false;
        }
        return minic_riscv64_emit_lvalue_store_to_address(
            file, program, target_id, target->type, "a0", "t4");
    }
    case MINIC_EXPRESSION_BUILTIN_VA_END: {
        const MinicExpression *target;
        MinicExpressionId target_id;

        target_id = expression->value.unary.operand;
        target = minic_c0_program_expression(program, target_id);
        return target != NULL && target->value_category == MINIC_VALUE_LVALUE &&
               minic_type_is_pointer(target->type) && !minic_type_is_const(target->type) &&
               minic_riscv64_emit_lvalue_address(
                   file, program, function, function_layout, target_id);
    }
    case MINIC_EXPRESSION_BUILTIN_UNREACHABLE:
''',
)

# Focused positive/negative contracts plus real RV64/QEMU execution through the
# ordinary runtime gate.
write(
    "tests/compiler/c0/gnu_builtin_va_start.c",
    r'''typedef __builtin_va_list va_list;

static int probe_va_start(int fixed, ...)
{
    va_list args;
    __builtin_va_start(args, fixed);
    if (!args)
        return 1;
    __builtin_va_end(args);
    return 0;
}

int main(void)
{
    return probe_va_start(7, 11, 13);
}
''',
)

write(
    "tests/compiler/c0/gnu_builtin_va_start_wrong_last.c",
    r'''typedef __builtin_va_list va_list;

static int bad_va_start(int first, int last, ...)
{
    va_list args;
    __builtin_va_start(args, first);
    __builtin_va_end(args);
    return last;
}
''',
)

write(
    "tests/compiler/c0/run-gnu-va-builtins.sh",
    r'''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/gnu-va-builtins
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_builtin_va_start.c" -o "$work/good.i"
"$minic" -S "$work/good.i" -o "$work/good.s"
test -s "$work/good.s"
grep -E 'add(i)?[[:space:]]+a0,[[:space:]]*s0' "$work/good.s" >/dev/null

"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_builtin_va_start_wrong_last.c" -o "$work/bad.i"
set +e
"$minic" -S "$work/bad.i" -o "$work/bad.s" 2>"$work/bad.err"
status=$?
set -e
test "$status" -ne 0
grep -F 'second argument must be the last named parameter' "$work/bad.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu-va-builtins start=semantic end=semantic wrong-last=fail-closed'
''',
)

replace_once(
    "tests/compiler/c0/run.sh",
    """MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-first500-pareto-v1.sh"
""",
    """MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-first500-pareto-v1.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-gnu-va-builtins.sh"
""",
)

replace_once(
    "tests/compiler/c0/run-runtime.sh",
    "run_case record_field_nonstring_attribute 0 record_field_nonstring_attribute\n",
    "run_case record_field_nonstring_attribute 0 record_field_nonstring_attribute\n"
    "run_case gnu_builtin_va_start 0 gnu_builtin_va_start\n",
)

print("GNU_VA_BUILTINS_V1_MATERIALIZED")
