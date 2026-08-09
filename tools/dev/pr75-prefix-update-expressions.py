#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    '''#define MINIC_UNARY_POST_INCREMENT ((MinicUnaryOperator)4)\n#define MINIC_UNARY_POST_DECREMENT ((MinicUnaryOperator)5)\n''',
    '''#define MINIC_UNARY_POST_INCREMENT ((MinicUnaryOperator)4)\n#define MINIC_UNARY_POST_DECREMENT ((MinicUnaryOperator)5)\n#define MINIC_UNARY_PRE_INCREMENT ((MinicUnaryOperator)6)\n#define MINIC_UNARY_PRE_DECREMENT ((MinicUnaryOperator)7)\n''',
    "prefix unary operator identities",
)

replace_once(
    "src/frontend/parser_expression.c",
    '''    if (parser->current.kind != MINIC_TOKEN_PLUS && parser->current.kind != MINIC_TOKEN_MINUS &&\n        parser->current.kind != MINIC_TOKEN_BANG && parser->current.kind != MINIC_TOKEN_TILDE &&\n        parser->current.kind != MINIC_TOKEN_AMPERSAND && parser->current.kind != MINIC_TOKEN_STAR) {\n''',
    '''    if (parser->current.kind != MINIC_TOKEN_PLUS && parser->current.kind != MINIC_TOKEN_MINUS &&\n        parser->current.kind != MINIC_TOKEN_BANG && parser->current.kind != MINIC_TOKEN_TILDE &&\n        parser->current.kind != MINIC_TOKEN_AMPERSAND && parser->current.kind != MINIC_TOKEN_STAR &&\n        parser->current.kind != MINIC_TOKEN_PLUS_PLUS &&\n        parser->current.kind != MINIC_TOKEN_MINUS_MINUS) {\n''',
    "prefix unary token dispatch",
)

replace_once(
    "src/frontend/parser_expression.c",
    '''    (void)memset(&expression, 0, sizeof(expression));\n    expression.span.begin = operator_token.span.begin;\n    expression.span.end = operand_expression->span.end;\n    expression.value.unary.operand = operand;\n\n    if (operator_token.kind == MINIC_TOKEN_AMPERSAND) {\n''',
    '''    (void)memset(&expression, 0, sizeof(expression));\n    expression.span.begin = operator_token.span.begin;\n    expression.span.end = operand_expression->span.end;\n    expression.value.unary.operand = operand;\n\n    if (operator_token.kind == MINIC_TOKEN_PLUS_PLUS ||\n        operator_token.kind == MINIC_TOKEN_MINUS_MINUS) {\n        MinicType pointee_type;\n\n        if (operand_expression->value_category != MINIC_VALUE_LVALUE ||\n            minic_type_is_const(operand_expression->type)) {\n            minic_parser_error(parser, "prefix update requires a modifiable scalar lvalue");\n            return false;\n        }\n        if (minic_type_is_pointer(operand_expression->type)) {\n            if (!minic_type_pointee(operand_expression->type, &pointee_type) ||\n                !minic_parser_require_complete_object_type(\n                    parser, pointee_type, "pointer update requires a complete object type")) {\n                return false;\n            }\n        } else if (!minic_type_is_integer(operand_expression->type)) {\n            minic_parser_error(parser, "prefix update requires integer or pointer lvalue");\n            return false;\n        }\n        expression.kind = MINIC_EXPRESSION_UNARY;\n        expression.type = operand_expression->type;\n        expression.value_category = MINIC_VALUE_RVALUE;\n        expression.value.unary.operator_kind =\n            operator_token.kind == MINIC_TOKEN_PLUS_PLUS ? MINIC_UNARY_PRE_INCREMENT\n                                                         : MINIC_UNARY_PRE_DECREMENT;\n        return minic_parser_add_expression(parser, &expression, expression_id);\n    }\n\n    if (operator_token.kind == MINIC_TOKEN_AMPERSAND) {\n''',
    "prefix unary construction",
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''static bool unary_operator_is_valid(MinicUnaryOperator operator_kind) {\n    return operator_kind >= MINIC_UNARY_PLUS && operator_kind <= MINIC_UNARY_POST_DECREMENT;\n}\n''',
    '''static bool unary_operator_is_valid(MinicUnaryOperator operator_kind) {\n    return operator_kind >= MINIC_UNARY_PLUS && operator_kind <= MINIC_UNARY_PRE_DECREMENT;\n}\n''',
    "prefix unary verifier range",
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''        if (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||\n            expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT) {\n''',
    '''        if (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||\n            expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||\n            expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||\n            expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT) {\n''',
    "prefix unary verifier semantics",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''static bool minic_riscv64_emit_postfix_update(FILE *file,\n                                              const MinicC0Program *program,\n                                              const MinicFunction *function,\n                                              const MinicExpression *expression) {\n    const MinicExpression *operand;\n    size_t element_size;\n    bool increment;\n\n    if (expression == NULL || expression->kind != MINIC_EXPRESSION_UNARY ||\n        (expression->value.unary.operator_kind != MINIC_UNARY_POST_INCREMENT &&\n         expression->value.unary.operator_kind != MINIC_UNARY_POST_DECREMENT)) {\n        return false;\n    }\n''',
    '''static bool minic_riscv64_emit_update(FILE *file,\n                                      const MinicC0Program *program,\n                                      const MinicFunction *function,\n                                      const MinicExpression *expression) {\n    const MinicExpression *operand;\n    size_t element_size;\n    bool increment;\n    bool prefix;\n\n    if (expression == NULL || expression->kind != MINIC_EXPRESSION_UNARY ||\n        (expression->value.unary.operator_kind != MINIC_UNARY_POST_INCREMENT &&\n         expression->value.unary.operator_kind != MINIC_UNARY_POST_DECREMENT &&\n         expression->value.unary.operator_kind != MINIC_UNARY_PRE_INCREMENT &&\n         expression->value.unary.operator_kind != MINIC_UNARY_PRE_DECREMENT)) {\n        return false;\n    }\n''',
    "generic RV64 update emitter",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''    increment = expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT;\n    element_size = 1U;\n''',
    '''    increment = expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||\n                expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT;\n    prefix = expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||\n             expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT;\n    element_size = 1U;\n''',
    "RV64 update mode",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''    return fprintf(file, "  ld t1, 0(sp)\\n") >= 0 &&\n           minic_riscv64_emit_scalar_store(file, operand->type, "t0", "t1") &&\n           fprintf(file, "  ld a0, 8(sp)\\n  addi sp, sp, 16\\n") >= 0;\n}\n''',
    '''    if (fprintf(file, "  ld t1, 0(sp)\\n") < 0 ||\n        !minic_riscv64_emit_scalar_store(file, operand->type, "t0", "t1")) {\n        return false;\n    }\n    return prefix ? fprintf(file, "  mv a0, t0\\n  addi sp, sp, 16\\n") >= 0\n                  : fprintf(file, "  ld a0, 8(sp)\\n  addi sp, sp, 16\\n") >= 0;\n}\n''',
    "RV64 prefix result semantics",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        if (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||\n            expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT) {\n            return minic_riscv64_emit_postfix_update(file, program, function, expression);\n        }\n''',
    '''        if (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||\n            expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||\n            expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||\n            expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT) {\n            return minic_riscv64_emit_update(file, program, function, expression);\n        }\n''',
    "RV64 prefix update dispatch",
)

print("staged prefix ++/-- expressions with shared RV64 update semantics")
