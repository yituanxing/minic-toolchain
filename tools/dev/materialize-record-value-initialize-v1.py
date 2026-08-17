#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]


def replace_once(relative_path: str, old: str, new: str) -> None:
    path = root / relative_path
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{relative_path}: anchor count={count}")
    path.write_text(text.replace(old, new, 1))


# Keep ordinary record assignment and record initialization as distinct semantic sinks.
replace_once(
    "src/frontend/ast.h",
    """    MINIC_STATEMENT_ASSIGN = 0,\n    MINIC_STATEMENT_RECORD_COPY,\n    MINIC_STATEMENT_XOR_ASSIGN,\n""",
    """    MINIC_STATEMENT_ASSIGN = 0,\n    MINIC_STATEMENT_RECORD_COPY,\n    MINIC_STATEMENT_RECORD_INITIALIZE,\n    MINIC_STATEMENT_XOR_ASSIGN,\n""",
)

# A record assignment expression is an rvalue producer whose value is backed by the
# just-written left operand.  The right operand must itself be a valid record producer.
replace_once(
    "src/frontend/ast.c",
    """    if (expression->kind == MINIC_EXPRESSION_CALL) {\n        return true;\n    }\n    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {\n""",
    """    if (expression->kind == MINIC_EXPRESSION_CALL) {\n        return true;\n    }\n    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT) {\n        const MinicExpression *left;\n        const MinicExpression *right;\n        MinicExpressionId left_id;\n        MinicExpressionId right_id;\n\n        left_id = expression->value.binary.left;\n        right_id = expression->value.binary.right;\n        if (left_id >= expression_id || right_id >= expression_id) {\n            return false;\n        }\n        left = minic_c0_program_expression(program, left_id);\n        right = minic_c0_program_expression(program, right_id);\n        return left != NULL && right != NULL &&\n               left->value_category == MINIC_VALUE_LVALUE && !minic_type_is_const(left->type) &&\n               minic_type_is_record(left->type) && minic_type_is_record(right->type) &&\n               left->type.record_id == expression->type.record_id &&\n               right->type.record_id == expression->type.record_id &&\n               minic_c0_record_value_is_copy_source_bounded(\n                   program, right_id, remaining - 1U);\n    }\n    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {\n""",
)

# The parser uses RECORD_INITIALIZE only for initialization-time copies.  Ordinary
# assignment keeps the existing modifiable-lvalue rule and RECORD_COPY representation.
replace_once(
    "src/frontend/parser_statement.c",
    """static bool add_record_copy_assignments(MinicParser *parser,\n                                        MinicExpressionId target_id,\n                                        MinicExpressionId source_id,\n                                        MinicSourceSpan span);\n""",
    """static bool add_record_copy_assignments(MinicParser *parser,\n                                        MinicExpressionId target_id,\n                                        MinicExpressionId source_id,\n                                        MinicSourceSpan span);\nstatic bool add_record_initializer_copy(MinicParser *parser,\n                                        MinicExpressionId target_id,\n                                        MinicExpressionId source_id,\n                                        MinicSourceSpan span);\n""",
)

replace_once(
    "src/frontend/parser_statement.c",
    """static bool add_record_copy_assignments(MinicParser *parser,\n                                        MinicExpressionId target_id,\n                                        MinicExpressionId source_id,\n                                        MinicSourceSpan span) {\n    const MinicExpression *target;\n    const MinicExpression *source;\n    const MinicRecord *record;\n    MinicStatement statement;\n\n    target = minic_c0_program_expression(parser->program, target_id);\n    source = minic_c0_program_expression(parser->program, source_id);\n    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||\n        !minic_c0_record_value_is_copy_source(parser->program, source_id) ||\n        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||\n        target->type.record_id != source->type.record_id || minic_type_is_const(target->type)) {\n        minic_parser_error(parser, \"record assignment requires a matching record copy source\");\n        return false;\n    }\n    record = minic_c0_program_record(parser->program, target->type.record_id);\n    if (record == NULL || !record->is_complete) {\n        minic_parser_error(parser, \"record assignment requires a complete record\");\n        return false;\n    }\n\n    (void)memset(&statement, 0, sizeof(statement));\n    statement.kind = MINIC_STATEMENT_RECORD_COPY;\n    statement.span = span;\n    statement.target_expression = target_id;\n    statement.expression = source_id;\n    statement.target_statement = MINIC_STATEMENT_INVALID;\n    statement.then_block = MINIC_BLOCK_INVALID;\n    statement.else_block = MINIC_BLOCK_INVALID;\n    return minic_parser_add_statement(parser, &statement);\n}\n""",
    """static bool add_record_copy_statement(MinicParser *parser,\n                                      MinicExpressionId target_id,\n                                      MinicExpressionId source_id,\n                                      MinicSourceSpan span,\n                                      MinicStatementKind statement_kind) {\n    const MinicExpression *target;\n    const MinicExpression *source;\n    const MinicRecord *record;\n    MinicStatement statement;\n    bool is_initializer;\n\n    is_initializer = statement_kind == MINIC_STATEMENT_RECORD_INITIALIZE;\n    if (!is_initializer && statement_kind != MINIC_STATEMENT_RECORD_COPY) {\n        return false;\n    }\n    target = minic_c0_program_expression(parser->program, target_id);\n    source = minic_c0_program_expression(parser->program, source_id);\n    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||\n        !minic_c0_record_value_is_copy_source(parser->program, source_id) ||\n        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||\n        target->type.record_id != source->type.record_id ||\n        (!is_initializer && minic_type_is_const(target->type))) {\n        minic_parser_error(\n            parser,\n            is_initializer ? \"record initializer requires a matching record copy source\"\n                           : \"record assignment requires a matching record copy source\");\n        return false;\n    }\n    record = minic_c0_program_record(parser->program, target->type.record_id);\n    if (record == NULL || !record->is_complete) {\n        minic_parser_error(\n            parser,\n            is_initializer ? \"record initializer requires a complete record\"\n                           : \"record assignment requires a complete record\");\n        return false;\n    }\n\n    (void)memset(&statement, 0, sizeof(statement));\n    statement.kind = statement_kind;\n    statement.span = span;\n    statement.target_expression = target_id;\n    statement.expression = source_id;\n    statement.target_statement = MINIC_STATEMENT_INVALID;\n    statement.then_block = MINIC_BLOCK_INVALID;\n    statement.else_block = MINIC_BLOCK_INVALID;\n    return minic_parser_add_statement(parser, &statement);\n}\n\nstatic bool add_record_copy_assignments(MinicParser *parser,\n                                        MinicExpressionId target_id,\n                                        MinicExpressionId source_id,\n                                        MinicSourceSpan span) {\n    return add_record_copy_statement(\n        parser, target_id, source_id, span, MINIC_STATEMENT_RECORD_COPY);\n}\n\nstatic bool add_record_initializer_copy(MinicParser *parser,\n                                        MinicExpressionId target_id,\n                                        MinicExpressionId source_id,\n                                        MinicSourceSpan span) {\n    return add_record_copy_statement(\n        parser, target_id, source_id, span, MINIC_STATEMENT_RECORD_INITIALIZE);\n}\n""",
)

replace_once(
    "src/frontend/parser_statement.c",
    """        return add_record_copy_assignments(parser, member_id, value_id, value->span);\n""",
    """        return add_record_initializer_copy(parser, member_id, value_id, value->span);\n""",
)
replace_once(
    "src/frontend/parser_statement.c",
    """                if (!add_record_copy_assignments(parser, target_id, source_id, source->span)) {\n""",
    """                if (!add_record_initializer_copy(parser, target_id, source_id, source->span)) {\n""",
)
replace_once(
    "src/frontend/parser_statement.c",
    """        if (!minic_c0_record_value_is_copy_source(parser->program, initializer_id) ||\n            !add_record_copy_assignments(parser, target_id, initializer_id, initializer_span)) {\n""",
    """        if (!minic_c0_record_value_is_copy_source(parser->program, initializer_id) ||\n            !add_record_initializer_copy(parser, target_id, initializer_id, initializer_span)) {\n""",
)

# The verifier is the semantic owner of whether a const destination may be written.
replace_once(
    "src/frontend/ast_verifier.c",
    """    case MINIC_STATEMENT_RECORD_COPY:\n        return target != NULL && expression != NULL &&\n               target->value_category == MINIC_VALUE_LVALUE &&\n               minic_c0_record_value_is_copy_source(program, statement->expression) &&\n               !minic_type_is_const(target->type) && minic_type_is_record(target->type) &&\n               minic_type_is_record(expression->type) &&\n               target->type.record_id == expression->type.record_id;\n""",
    """    case MINIC_STATEMENT_RECORD_COPY:\n    case MINIC_STATEMENT_RECORD_INITIALIZE:\n        return target != NULL && expression != NULL &&\n               target->value_category == MINIC_VALUE_LVALUE &&\n               minic_c0_record_value_is_copy_source(program, statement->expression) &&\n               (statement->kind == MINIC_STATEMENT_RECORD_INITIALIZE ||\n                !minic_type_is_const(target->type)) &&\n               minic_type_is_record(target->type) && minic_type_is_record(expression->type) &&\n               target->type.record_id == expression->type.record_id;\n""",
)

# The target helper performs a byte copy.  Language-level const legality is already
# guaranteed by the verified statement/expression semantic owner above it.
replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||\n        minic_type_is_const(target->type) || !minic_type_is_record(target->type) ||\n        !minic_type_is_record(source->type) || target->type.record_id != source->type.record_id ||\n        !minic_c0_record_value_is_copy_source(program, source_id)) {\n""",
    """    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||\n        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||\n        target->type.record_id != source->type.record_id ||\n        !minic_c0_record_value_is_copy_source(program, source_id)) {\n""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    if (!minic_c0_record_value_is_address_backed(program, expression_id)) {\n        return false;\n    }\n    expression = minic_c0_program_expression(program, expression_id);\n    if (expression == NULL) {\n        return false;\n    }\n    if (expression->value_category == MINIC_VALUE_LVALUE) {\n        return minic_riscv64_emit_lvalue_address(\n            file, program, function, function_layout, expression_id);\n    }\n    return expression->kind == MINIC_EXPRESSION_STATEMENT &&\n           minic_riscv64_emit_expression(file, program, function, function_layout, expression_id);\n""",
    """    expression = minic_c0_program_expression(program, expression_id);\n    if (expression == NULL || !minic_type_is_record(expression->type)) {\n        return false;\n    }\n    if (minic_c0_record_value_is_address_backed(program, expression_id)) {\n        if (expression->value_category == MINIC_VALUE_LVALUE) {\n            return minic_riscv64_emit_lvalue_address(\n                file, program, function, function_layout, expression_id);\n        }\n        return expression->kind == MINIC_EXPRESSION_STATEMENT &&\n               minic_riscv64_emit_expression(\n                   file, program, function, function_layout, expression_id);\n    }\n    return expression->kind == MINIC_EXPRESSION_ASSIGNMENT &&\n           expression->value_category == MINIC_VALUE_RVALUE &&\n           minic_c0_record_value_is_copy_source(program, expression_id) &&\n           minic_riscv64_emit_expression(\n               file, program, function, function_layout, expression_id);\n""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    if (minic_c0_record_value_is_address_backed(program, source_id)) {\n        size_t index;\n\n        if (!minic_riscv64_emit_address_backed_record_value(\n""",
    """    if (minic_c0_record_value_is_address_backed(program, source_id) ||\n        source->kind == MINIC_EXPRESSION_ASSIGNMENT) {\n        size_t index;\n\n        if (!minic_riscv64_emit_address_backed_record_value(\n""",
)

replace_once(
    "src/target/riscv64/codegen_statement.c",
    """    case MINIC_STATEMENT_RECORD_COPY:\n        return minic_riscv64_emit_record_copy(file, program, function, function_layout, statement);\n\n    case MINIC_STATEMENT_XOR_ASSIGN:\n""",
    """    case MINIC_STATEMENT_RECORD_COPY:\n    case MINIC_STATEMENT_RECORD_INITIALIZE:\n        return minic_riscv64_emit_record_copy(file, program, function, function_layout, statement);\n\n    case MINIC_STATEMENT_XOR_ASSIGN:\n""",
)
