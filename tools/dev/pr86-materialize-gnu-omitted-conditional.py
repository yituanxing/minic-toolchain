#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    file_path = Path(path)
    text = file_path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact anchor, found {count}")
    file_path.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    '''        struct {
            MinicExpressionId condition;
            MinicExpressionId when_true;
            MinicExpressionId when_false;
        } conditional;
''',
    '''        struct {
            MinicExpressionId condition;
            MinicExpressionId when_true;
            MinicExpressionId when_false;
            bool uses_condition_value;
        } conditional;
''',
    "conditional semantic flag",
)

replace_once(
    "src/frontend/parser_expression.c",
    '''        MinicExpressionId when_true;
        MinicExpressionId when_false;
''',
    '''        MinicExpressionId when_true;
        MinicExpressionId when_false;
        bool uses_condition_value;
''',
    "conditional parser state",
)

replace_once(
    "src/frontend/parser_expression.c",
    '''        condition_span = condition_expression->span;
        if (!minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &when_true, 0U, true) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_COLON, "expected ':' in conditional expression") ||
            !parse_expression_internal(parser, &when_false, 0U, true)) {
            return false;
        }
''',
    '''        condition_span = condition_expression->span;
        uses_condition_value = false;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_COLON) {
            when_true = left;
            uses_condition_value = true;
            if (!minic_parser_expect(
                    parser, MINIC_TOKEN_COLON, "expected ':' in conditional expression") ||
                !parse_expression_internal(parser, &when_false, 0U, true)) {
                return false;
            }
        } else if (!parse_expression_internal(parser, &when_true, 0U, true) ||
                   !minic_parser_expect(
                       parser, MINIC_TOKEN_COLON, "expected ':' in conditional expression") ||
                   !parse_expression_internal(parser, &when_false, 0U, true)) {
            return false;
        }
''',
    "GNU omitted-middle parser branch",
)

replace_once(
    "src/frontend/parser_expression.c",
    '''        conditional.value.conditional.condition = left;
        conditional.value.conditional.when_true = when_true;
        conditional.value.conditional.when_false = when_false;
''',
    '''        conditional.value.conditional.condition = left;
        conditional.value.conditional.when_true = when_true;
        conditional.value.conditional.when_false = when_false;
        conditional.value.conditional.uses_condition_value = uses_condition_value;
''',
    "persist omitted-middle semantics",
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''        return condition != NULL && when_true != NULL && when_false != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               type_is_condition_scalar(condition->type) &&
               conditional_result_type(when_true->type, when_false->type, &expected_type) &&
               minic_type_equal(expression->type, expected_type);
''',
    '''        return condition != NULL && when_true != NULL && when_false != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               type_is_condition_scalar(condition->type) &&
               (!expression->value.conditional.uses_condition_value ||
                expression->value.conditional.when_true ==
                    expression->value.conditional.condition) &&
               conditional_result_type(when_true->type, when_false->type, &expected_type) &&
               minic_type_equal(expression->type, expected_type);
''',
    "verify condition-value identity",
)

replace_once(
    "src/frontend/const_eval.c",
    '''        selected_id = truthy ? expression->value.conditional.when_true
                             : expression->value.conditional.when_false;
        return eval_expression(program, target, selected_id, depth + 1U, &selected) &&
               convert_value(program, target, &selected, expression->type, value);
''',
    '''        if (truthy && expression->value.conditional.uses_condition_value) {
            return convert_value(program, target, &condition, expression->type, value);
        }
        selected_id = truthy ? expression->value.conditional.when_true
                             : expression->value.conditional.when_false;
        return eval_expression(program, target, selected_id, depth + 1U, &selected) &&
               convert_value(program, target, &selected, expression->type, value);
''',
    "reuse const-evaluated condition value",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        if (condition == NULL || when_true == NULL || when_false == NULL ||
            !type_is_condition_scalar(condition->type) ||
            !minic_riscv64_emit_expression(
                file, program, function, expression->value.conditional.condition) ||
            fprintf(file, "  beqz a0, .Lminic_cond_false_%zu\\n", expression_id) < 0 ||
            !minic_riscv64_emit_expression(
                file, program, function, expression->value.conditional.when_true) ||
            !minic_riscv64_emit_conditional_result_conversion(
                file, when_true->type, expression->type) ||
            fprintf(file,
                    "  j .Lminic_cond_end_%zu\\n"
                    ".Lminic_cond_false_%zu:\\n",
                    expression_id,
                    expression_id) < 0 ||
            !minic_riscv64_emit_expression(
                file, program, function, expression->value.conditional.when_false) ||
            !minic_riscv64_emit_conditional_result_conversion(
                file, when_false->type, expression->type)) {
            return false;
        }
''',
    '''        if (condition == NULL || when_true == NULL || when_false == NULL ||
            !type_is_condition_scalar(condition->type) ||
            (expression->value.conditional.uses_condition_value &&
             expression->value.conditional.when_true !=
                 expression->value.conditional.condition) ||
            !minic_riscv64_emit_expression(
                file, program, function, expression->value.conditional.condition) ||
            fprintf(file, "  beqz a0, .Lminic_cond_false_%zu\\n", expression_id) < 0) {
            return false;
        }
        if (expression->value.conditional.uses_condition_value) {
            if (!minic_riscv64_emit_conditional_result_conversion(
                    file, condition->type, expression->type)) {
                return false;
            }
        } else if (!minic_riscv64_emit_expression(
                       file, program, function, expression->value.conditional.when_true) ||
                   !minic_riscv64_emit_conditional_result_conversion(
                       file, when_true->type, expression->type)) {
            return false;
        }
        if (fprintf(file,
                    "  j .Lminic_cond_end_%zu\\n"
                    ".Lminic_cond_false_%zu:\\n",
                    expression_id,
                    expression_id) < 0 ||
            !minic_riscv64_emit_expression(
                file, program, function, expression->value.conditional.when_false) ||
            !minic_riscv64_emit_conditional_result_conversion(
                file, when_false->type, expression->type)) {
            return false;
        }
''',
    "single-evaluation RV64 lowering",
)

Path("tests/compiler/c0/gnu_omitted_conditional.c").write_text(
    '''static int probe_calls;

static int probe(int value)
{
    probe_calls += 1;
    return value;
}

int linux_shape(int ret)
{
    return ret ?: -7;
}

int evaluate_once(void)
{
    return probe(5) ?: 9;
}

int false_fallback(void)
{
    return 0 ?: 9;
}

_Static_assert((5 ?: 9) == 5, "GNU omitted conditional true consteval");
_Static_assert((0 ?: 9) == 9, "GNU omitted conditional false consteval");

int main(void)
{
    return linux_shape(3) + false_fallback() + evaluate_once() + probe_calls == 18 ? 0 : 1;
}
'''
)

Path("tests/compiler/c0/run-gnu-omitted-conditional.sh").write_text(
    '''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-omitted-conditional

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_omitted_conditional.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

grep -F 'linux_shape:' "$work/output.s" >/dev/null
grep -F 'evaluate_once:' "$work/output.s" >/dev/null
grep -F 'false_fallback:' "$work/output.s" >/dev/null
test "$(grep -c -F '  call probe' "$work/output.s")" -eq 1
grep -F 'beqz a0, .Lminic_cond_false_' "$work/output.s" >/dev/null
grep -F 'li a0, -7' "$work/output.s" >/dev/null
grep -F 'li a0, 9' "$work/output.s" >/dev/null

printf '%s\n' \
  'PASS compiler/c0/gnu_omitted_conditional linux-shape=1 condition-evaluated-once=1 false-fallback=1 typed-consteval=true+false ordinary-conditional=unchanged'
'''
)

run_path = Path("tests/compiler/c0/run.sh")
run_text = run_path.read_text()
needle = 'sh "$root/tests/compiler/c0/run-gnu-void-return-expression.sh"\n'
if needle not in run_text:
    raise SystemExit("C0 runner insertion anchor missing")
insert = needle + '''\nMINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-gnu-omitted-conditional.sh"\n'''
run_path.write_text(run_text.replace(needle, insert, 1))
