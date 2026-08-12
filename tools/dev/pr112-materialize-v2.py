from pathlib import Path

# First apply the v0 edits and focused tests.
exec(compile(Path("tools/dev/pr112-materialize.py").read_text(), "tools/dev/pr112-materialize.py", "exec"))


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"anchor mismatch {path}: {count}")
    p.write_text(text.replace(old, new, 1))


def remove_between(path: str, start: str, end: str) -> None:
    p = Path(path)
    text = p.read_text()
    begin = text.find(start)
    finish = text.find(end, begin + len(start))
    if begin < 0 or finish < 0:
        raise SystemExit(f"range anchor mismatch {path}: {begin}, {finish}")
    p.write_text(text[:begin] + text[finish:])


# The conditional result is an expression-semantic query, not a parser/verifier-private rule.
replace_once(
    "src/frontend/ast.h",
    "bool minic_c0_expression_is_null_pointer_constant_v0(const MinicC0Program *program,\n                                                          MinicExpressionId expression_id);",
    "bool minic_c0_expression_is_null_pointer_constant_v0(const MinicC0Program *program,\n                                                          MinicExpressionId expression_id);\nbool minic_c0_conditional_result_type(const MinicC0Program *program,\n                                      MinicExpressionId when_true_expression_id,\n                                      MinicExpressionId when_false_expression_id,\n                                      MinicType *result);",
)

npc_end = '''    return operand != NULL && operand->kind == MINIC_EXPRESSION_INTEGER &&
           minic_type_is_integer(operand->type) && operand->value.integer_value == 0;
}
'''
shared = r'''
static bool minic_c0_conditional_type_only(MinicType when_true,
                                           MinicType when_false,
                                           MinicType *result) {
    bool has_double_operand;
    bool has_numeric_operands;

    if (result == NULL) {
        return false;
    }
    if (minic_type_equal(when_true, when_false)) {
        *result = when_true;
        return true;
    }
    if (minic_type_conditional_pointer_common(when_true, when_false, result)) {
        return true;
    }
    if (minic_type_is_integer(when_true) && minic_type_is_integer(when_false)) {
        return minic_type_integer_common(when_true, when_false, result);
    }
    has_double_operand = minic_type_is_double(when_true) || minic_type_is_double(when_false);
    has_numeric_operands = (minic_type_is_double(when_true) || minic_type_is_integer(when_true)) &&
                           (minic_type_is_double(when_false) || minic_type_is_integer(when_false));
    if (has_double_operand && has_numeric_operands) {
        *result = minic_type_double();
        return true;
    }
    return false;
}

bool minic_c0_conditional_result_type(const MinicC0Program *program,
                                      MinicExpressionId when_true_expression_id,
                                      MinicExpressionId when_false_expression_id,
                                      MinicType *result) {
    const MinicExpression *when_true;
    const MinicExpression *when_false;

    if (program == NULL || result == NULL) {
        return false;
    }
    when_true = minic_c0_program_expression(program, when_true_expression_id);
    when_false = minic_c0_program_expression(program, when_false_expression_id);
    if (when_true == NULL || when_false == NULL) {
        return false;
    }
    if (minic_type_is_pointer(when_true->type) &&
        minic_c0_expression_is_null_pointer_constant_v0(program, when_false_expression_id)) {
        *result = when_true->type;
        return true;
    }
    if (minic_c0_expression_is_null_pointer_constant_v0(program, when_true_expression_id) &&
        minic_type_is_pointer(when_false->type)) {
        *result = when_false->type;
        return true;
    }
    return minic_c0_conditional_type_only(when_true->type, when_false->type, result);
}
'''
replace_once("src/frontend/ast.c", npc_end, npc_end + shared)

# Remove both duplicate type-only services.
remove_between(
    "src/frontend/parser_expression.c",
    "static bool conditional_result_type(",
    "static bool binary_result_type(",
)
remove_between(
    "src/frontend/ast_verifier.c",
    "static bool conditional_result_type(",
    "static bool is_normalized_integer_cast_add(",
)

# Parser consumes the shared expression-aware service.
old_parser = '''        if (minic_type_is_pointer(true_expression->type) &&
            minic_c0_expression_is_null_pointer_constant_v0(parser->program, when_false)) {
            conditional.type = true_expression->type;
        } else if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, when_true) &&
                   minic_type_is_pointer(false_expression->type)) {
            conditional.type = false_expression->type;
        } else if (!conditional_result_type(
                       true_expression->type, false_expression->type, &conditional.type)) {
            minic_parser_error(parser, "conditional expression branches have incompatible types");
            return false;
        }
'''
new_parser = '''        if (!minic_c0_conditional_result_type(
                parser->program, when_true, when_false, &conditional.type)) {
            minic_parser_error(parser, "conditional expression branches have incompatible types");
            return false;
        }
'''
replace_once("src/frontend/parser_expression.c", old_parser, new_parser)

# Verifier consumes the exact same service, eliminating parser/verifier semantic drift.
replace_once(
    "src/frontend/ast_verifier.c",
    "               conditional_result_type(when_true->type, when_false->type, &expected_type) &&\n               minic_type_equal(expression->type, expected_type);",
    "               minic_c0_conditional_result_type(program,\n                                                expression->value.conditional.when_true,\n                                                expression->value.conditional.when_false,\n                                                &expected_type) &&\n               minic_type_equal(expression->type, expected_type);",
)
