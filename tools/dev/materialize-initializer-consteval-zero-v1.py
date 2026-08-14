#!/usr/bin/env python3
from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text.rstrip() + "\n", encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


path = "src/frontend/parser_statement.c"
text = read(path)
old = """static bool aggregate_expression_is_zero_constant(const MinicC0Program *program,
                                                  MinicExpressionId expression_id) {
    const MinicExpression *expression;

    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_INTEGER) {
        return minic_type_is_integer(expression->type) && expression->value.integer_value == 0;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST && minic_type_is_pointer(expression->type)) {
        return aggregate_expression_is_zero_constant(program, expression->value.unary.operand);
    }
    return false;
}
"""
new = """static bool aggregate_expression_is_zero_constant(const MinicParser *parser,
                                                  MinicExpressionId expression_id) {
    MinicConstValue constant;
    const MinicExpression *expression;
    bool is_zero;

    if (parser == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST && minic_type_is_pointer(expression->type)) {
        return aggregate_expression_is_zero_constant(parser, expression->value.unary.operand);
    }
    return minic_type_is_integer(expression->type) &&
           minic_const_eval_integer(
               parser->program, parser->target_info, expression_id, &constant) &&
           minic_const_value_is_zero(
               parser->program, parser->target_info, &constant, &is_zero) &&
           is_zero;
}
"""
text = replace_once(text, old, new, "aggregate zero predicate")
text = replace_once(
    text,
    "!aggregate_expression_is_zero_constant(parser->program, value_id)",
    "!aggregate_expression_is_zero_constant(parser, value_id)",
    "aggregate zero predicate caller",
)
write(path, text)

path = "tests/compiler/c0/zero_aggregate_null.c"
text = read(path)
old = """int main(void) {
    struct Pair value = {0, ((void *)0)};
    return value.count;
}
"""
new = """int main(void) {
    struct Pair value = {1 - 1, ((void *)(2 - 2))};
    return value.count;
}
"""
text = replace_once(text, old, new, "zero aggregate constant-expression regression")
write(path, text)

path = "tests/compiler/c0/run-zero-aggregate-null.sh"
text = read(path)
text = replace_once(
    text,
    "PASS compiler/c0/zero_aggregate_null scalar-zero=yes pointer-null=yes",
    "PASS compiler/c0/zero_aggregate_null consteval-zero=yes pointer-null=yes",
    "zero aggregate test signature",
)
write(path, text)

print("MATERIALIZED initializer-consteval-zero-v1")
