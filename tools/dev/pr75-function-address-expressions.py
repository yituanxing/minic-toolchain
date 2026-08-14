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
    "src/frontend/parser_expression.c",
    '''    if (operator_token.kind == MINIC_TOKEN_AMPERSAND) {\n        if (local_array_without_array_type(parser, operand_expression)) {\n            minic_parser_error(parser, "address-of local array object is not supported yet");\n            return false;\n        }\n        if (operand_expression->value_category != MINIC_VALUE_LVALUE ||\n            !minic_type_pointer_to(operand_expression->type, &expression.type)) {\n            minic_parser_error(parser, "address-of requires an lvalue operand");\n            return false;\n        }\n        expression.kind = MINIC_EXPRESSION_ADDRESS_OF;\n        expression.value_category = MINIC_VALUE_RVALUE;\n        return minic_parser_add_expression(parser, &expression, expression_id);\n    }\n''',
    '''    if (operator_token.kind == MINIC_TOKEN_AMPERSAND) {\n        MinicType function_type;\n\n        if (operand_expression->kind == MINIC_EXPRESSION_FUNCTION &&\n            minic_type_pointee(operand_expression->type, &function_type) &&\n            minic_type_is_function(function_type)) {\n            expression.type = operand_expression->type;\n        } else if (minic_type_is_function(operand_expression->type)) {\n            if (!minic_type_pointer_to(operand_expression->type, &expression.type)) {\n                minic_parser_error(parser, "cannot form pointer to function designator");\n                return false;\n            }\n        } else {\n            if (local_array_without_array_type(parser, operand_expression)) {\n                minic_parser_error(parser, "address-of local array object is not supported yet");\n                return false;\n            }\n            if (operand_expression->value_category != MINIC_VALUE_LVALUE ||\n                !minic_type_pointer_to(operand_expression->type, &expression.type)) {\n                minic_parser_error(parser, "address-of requires an lvalue object or function designator");\n                return false;\n            }\n        }\n        expression.kind = MINIC_EXPRESSION_ADDRESS_OF;\n        expression.value_category = MINIC_VALUE_RVALUE;\n        return minic_parser_add_expression(parser, &expression, expression_id);\n    }\n''',
    "function address parser",
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''    case MINIC_EXPRESSION_ADDRESS_OF: {\n        MinicType pointee_type;\n\n        operand = expression_before(program, expression->value.unary.operand, expression_index);\n        return operand != NULL && operand->value_category == MINIC_VALUE_LVALUE &&\n               expression->value_category == MINIC_VALUE_RVALUE &&\n               minic_type_pointee(expression->type, &pointee_type) &&\n               minic_type_equal(pointee_type, operand->type);\n    }\n''',
    '''    case MINIC_EXPRESSION_ADDRESS_OF: {\n        MinicType pointee_type;\n        MinicType function_type;\n\n        operand = expression_before(program, expression->value.unary.operand, expression_index);\n        if (operand == NULL || expression->value_category != MINIC_VALUE_RVALUE) {\n            return false;\n        }\n        if (operand->kind == MINIC_EXPRESSION_FUNCTION) {\n            return minic_type_equal(expression->type, operand->type) &&\n                   minic_type_pointee(operand->type, &function_type) &&\n                   minic_type_is_function(function_type);\n        }\n        if (minic_type_is_function(operand->type)) {\n            return minic_type_pointee(expression->type, &pointee_type) &&\n                   minic_type_equal(pointee_type, operand->type);\n        }\n        return operand->value_category == MINIC_VALUE_LVALUE &&\n               minic_type_pointee(expression->type, &pointee_type) &&\n               minic_type_equal(pointee_type, operand->type);\n    }\n''',
    "function address verifier",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''    case MINIC_EXPRESSION_ADDRESS_OF:\n        return minic_riscv64_emit_lvalue_address(\n            file, program, function, expression->value.unary.operand);\n''',
    '''    case MINIC_EXPRESSION_ADDRESS_OF: {\n        const MinicExpression *operand;\n\n        operand = minic_c0_program_expression(program, expression->value.unary.operand);\n        if (operand == NULL) {\n            return false;\n        }\n        if (operand->kind == MINIC_EXPRESSION_FUNCTION || minic_type_is_function(operand->type)) {\n            return minic_riscv64_emit_expression(\n                file, program, function, expression->value.unary.operand);\n        }\n        return minic_riscv64_emit_lvalue_address(\n            file, program, function, expression->value.unary.operand);\n    }\n''',
    "function address RV64 codegen",
)

print("staged address-of for function designators and &*fp")
