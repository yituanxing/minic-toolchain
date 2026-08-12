from pathlib import Path

# Apply v0 + shared Parser/Verifier conditional semantics first.
exec(compile(Path("tools/dev/pr112-materialize-v2.py").read_text(), "tools/dev/pr112-materialize-v2.py", "exec"))


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"anchor mismatch {path}: {count}")
    p.write_text(text.replace(old, new, 1))


# A null-pointer arm needs an explicit semantic conversion after result typing.
# The cast normalizer already lowers integer-zero->pointer and pointer->pointer
# casts to BITCAST, so RV64 receives uniform pointer-typed branches without
# learning C null-pointer-constant rules.
anchor = "static bool binary_result_type(const MinicC0Program *program,"
helper = r'''static bool normalize_conditional_null_pointer_arm(MinicParser *parser,
                                                   MinicExpressionId *arm_id,
                                                   MinicType result_type) {
    const MinicExpression *arm;
    MinicExpression conversion;
    MinicExpressionId converted_id;

    if (parser == NULL || arm_id == NULL) {
        return false;
    }
    arm = minic_c0_program_expression(parser->program, *arm_id);
    if (arm == NULL) {
        return false;
    }
    if (minic_type_equal(arm->type, result_type) ||
        !minic_type_is_pointer(result_type) ||
        !minic_c0_expression_is_null_pointer_constant_v0(parser->program, *arm_id)) {
        return true;
    }

    (void)memset(&conversion, 0, sizeof(conversion));
    conversion.kind = MINIC_EXPRESSION_CAST;
    conversion.span = arm->span;
    conversion.type = result_type;
    conversion.value_category = MINIC_VALUE_RVALUE;
    conversion.value.unary.operand = *arm_id;
    if (!minic_parser_add_expression(parser, &conversion, &converted_id)) {
        return false;
    }
    *arm_id = converted_id;
    return true;
}

'''
replace_once("src/frontend/parser_expression.c", anchor, helper + anchor)

old = '''        if (!minic_c0_conditional_result_type(
                parser->program, when_true, when_false, &conditional.type)) {
            minic_parser_error(parser, "conditional expression branches have incompatible types");
            return false;
        }
        if (!minic_parser_add_expression(parser, &conditional, &left)) {
            return false;
        }
'''
new = '''        if (!minic_c0_conditional_result_type(
                parser->program, when_true, when_false, &conditional.type)) {
            minic_parser_error(parser, "conditional expression branches have incompatible types");
            return false;
        }
        if (!normalize_conditional_null_pointer_arm(parser, &when_true, conditional.type) ||
            !normalize_conditional_null_pointer_arm(parser, &when_false, conditional.type)) {
            minic_parser_error(parser, "cannot normalize conditional null pointer arm");
            return false;
        }
        conditional.value.conditional.when_true = when_true;
        conditional.value.conditional.when_false = when_false;
        if (!minic_parser_add_expression(parser, &conditional, &left)) {
            return false;
        }
'''
replace_once("src/frontend/parser_expression.c", old, new)
