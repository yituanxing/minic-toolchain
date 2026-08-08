#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file_path = Path(path)
    text = file_path.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one replacement site, found {count}")
    file_path.write_text(text.replace(old, new, 1))


def insert_before(path: str, marker: str, insertion: str) -> None:
    replace_once(path, marker, insertion + marker)


parser_helper = r'''static bool conditional_result_type(MinicType when_true,
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
    if (minic_type_is_integer(when_true) && minic_type_is_integer(when_false)) {
        return minic_type_integer_common(when_true, when_false, result);
    }
    has_double_operand = minic_type_is_double(when_true) || minic_type_is_double(when_false);
    has_numeric_operands =
        (minic_type_is_double(when_true) || minic_type_is_integer(when_true)) &&
        (minic_type_is_double(when_false) || minic_type_is_integer(when_false));
    if (has_double_operand && has_numeric_operands) {
        *result = minic_type_double();
        return true;
    }
    return false;
}

'''
insert_before(
    "src/frontend/parser_expression.c",
    "static bool binary_result_type(const MinicC0Program *program,\n",
    parser_helper,
)

conditional_parse = r'''    if (minimum_precedence == 0U && parser->current.kind == MINIC_TOKEN_QUESTION) {
        const MinicExpression *condition_expression;
        const MinicExpression *true_expression;
        const MinicExpression *false_expression;
        MinicExpression conditional;
        MinicExpressionId when_true;
        MinicExpressionId when_false;

        if (!minic_parser_apply_array_decay(parser, left, &left)) {
            return false;
        }
        condition_expression = minic_c0_program_expression(parser->program, left);
        if (condition_expression == NULL || !type_is_condition_scalar(condition_expression->type)) {
            minic_parser_error(parser, "conditional expression requires an integer or pointer condition");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &when_true, 0U, true) ||
            !minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' in conditional expression") ||
            !parse_expression_internal(parser, &when_false, 0U, true)) {
            return false;
        }
        true_expression = minic_c0_program_expression(parser->program, when_true);
        false_expression = minic_c0_program_expression(parser->program, when_false);
        if (true_expression == NULL || false_expression == NULL) {
            minic_parser_error(parser, "invalid conditional expression operand");
            return false;
        }

        (void)memset(&conditional, 0, sizeof(conditional));
        conditional.kind = MINIC_EXPRESSION_CONDITIONAL;
        conditional.span.begin = condition_expression->span.begin;
        conditional.span.end = false_expression->span.end;
        conditional.value_category = MINIC_VALUE_RVALUE;
        conditional.value.conditional.condition = left;
        conditional.value.conditional.when_true = when_true;
        conditional.value.conditional.when_false = when_false;
        if (!conditional_result_type(
                true_expression->type, false_expression->type, &conditional.type)) {
            minic_parser_error(parser, "conditional expression branches have incompatible types");
            return false;
        }
        if (!minic_parser_add_expression(parser, &conditional, &left)) {
            return false;
        }
    }
'''
replace_once(
    "src/frontend/parser_expression.c",
    "    *expression_id = left;\n    return true;\n}\n\nbool minic_parser_parse_expression",
    conditional_parse
    + "    *expression_id = left;\n    return true;\n}\n\nbool minic_parser_parse_expression",
)

verifier_helper = r'''static bool conditional_result_type(MinicType when_true,
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
    if (minic_type_is_integer(when_true) && minic_type_is_integer(when_false)) {
        return minic_type_integer_common(when_true, when_false, result);
    }
    has_double_operand = minic_type_is_double(when_true) || minic_type_is_double(when_false);
    has_numeric_operands =
        (minic_type_is_double(when_true) || minic_type_is_integer(when_true)) &&
        (minic_type_is_double(when_false) || minic_type_is_integer(when_false));
    if (has_double_operand && has_numeric_operands) {
        *result = minic_type_double();
        return true;
    }
    return false;
}

'''
insert_before(
    "src/frontend/ast_verifier.c",
    "static bool is_normalized_integer_cast_add(const MinicExpression *expression,\n",
    verifier_helper,
)

conditional_verify = r'''    case MINIC_EXPRESSION_CONDITIONAL: {
        const MinicExpression *condition;
        const MinicExpression *when_true;
        const MinicExpression *when_false;
        MinicType expected_type;

        condition = expression_before(
            program, expression->value.conditional.condition, expression_index);
        when_true = expression_before(
            program, expression->value.conditional.when_true, expression_index);
        when_false = expression_before(
            program, expression->value.conditional.when_false, expression_index);
        return condition != NULL && when_true != NULL && when_false != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               type_is_condition_scalar(condition->type) &&
               conditional_result_type(when_true->type, when_false->type, &expected_type) &&
               minic_type_equal(expression->type, expected_type);
    }
'''
insert_before(
    "src/frontend/ast_verifier.c",
    "    case MINIC_EXPRESSION_CALL:\n",
    conditional_verify,
)

conditional_remap = r'''    case MINIC_EXPRESSION_CONDITIONAL:
        return remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.conditional.condition,
                                   &expression->value.conditional.condition) &&
               remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.conditional.when_true,
                                   &expression->value.conditional.when_true) &&
               remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.conditional.when_false,
                                   &expression->value.conditional.when_false);
'''
insert_before(
    "src/frontend/cast_normalization.c",
    "    case MINIC_EXPRESSION_CALL:\n",
    conditional_remap,
)

codegen_helper = r'''static bool minic_riscv64_emit_conditional_result_conversion(FILE *file,
                                                             MinicType source_type,
                                                             MinicType result_type) {
    if (minic_type_equal(source_type, result_type)) {
        return true;
    }
    if (minic_type_is_integer(source_type) && minic_type_is_integer(result_type)) {
        return minic_riscv64_emit_integer_conversion(file, result_type, "a0");
    }
    if (minic_type_is_integer(source_type) && minic_type_is_double(result_type)) {
        return minic_riscv64_emit_integer_to_double(file, source_type, "a0", "ft0") &&
               fprintf(file, "  fmv.x.d a0, ft0\\n") >= 0;
    }
    return false;
}

'''
insert_before(
    "src/target/riscv64/codegen_expression.c",
    "static bool minic_riscv64_emit_scale_register(FILE *file,\n",
    codegen_helper,
)

conditional_codegen = r'''    case MINIC_EXPRESSION_CONDITIONAL: {
        const MinicExpression *condition;
        const MinicExpression *when_true;
        const MinicExpression *when_false;

        condition = minic_c0_program_expression(program, expression->value.conditional.condition);
        when_true = minic_c0_program_expression(program, expression->value.conditional.when_true);
        when_false = minic_c0_program_expression(program, expression->value.conditional.when_false);
        if (condition == NULL || when_true == NULL || when_false == NULL ||
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
        return fprintf(file, ".Lminic_cond_end_%zu:\\n", expression_id) >= 0;
    }
'''
insert_before(
    "src/target/riscv64/codegen_expression.c",
    "    case MINIC_EXPRESSION_CALL: {\n",
    conditional_codegen,
)

print("patched conditional expression support")
