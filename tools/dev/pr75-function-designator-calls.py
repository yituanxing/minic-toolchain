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
    "src/frontend/parser_postfix.c",
    '''    callee = minic_c0_program_expression(parser->program, callee_id);\n    if (callee == NULL || !minic_type_pointee(callee->type, &function_type) ||\n        !minic_type_is_function(function_type)) {\n        return NULL;\n    }\n    return minic_c0_program_function_type(parser->program, function_type.function_type_id);\n''',
    '''    callee = minic_c0_program_expression(parser->program, callee_id);\n    if (callee == NULL) {\n        return NULL;\n    }\n    function_type = callee->type;\n    if (!minic_type_is_function(function_type) &&\n        (!minic_type_pointee(callee->type, &function_type) ||\n         !minic_type_is_function(function_type))) {\n        return NULL;\n    }\n    return minic_c0_program_function_type(parser->program, function_type.function_type_id);\n''',
    "indirect parser function designator",
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''            const MinicFunctionType *function_type;\n            MinicType pointee;\n\n            callee = expression_before(program, expression->value.call.callee, expression_index);\n            if (callee == NULL || !minic_type_pointee(callee->type, &pointee) ||\n                !minic_type_is_function(pointee)) {\n                return false;\n            }\n            function_type = minic_c0_program_function_type(program, pointee.function_type_id);\n''',
    '''            const MinicFunctionType *function_type;\n            MinicType callee_type;\n\n            callee = expression_before(program, expression->value.call.callee, expression_index);\n            if (callee == NULL) {\n                return false;\n            }\n            callee_type = callee->type;\n            if (!minic_type_is_function(callee_type) &&\n                (!minic_type_pointee(callee->type, &callee_type) ||\n                 !minic_type_is_function(callee_type))) {\n                return false;\n            }\n            function_type = minic_c0_program_function_type(program, callee_type.function_type_id);\n''',
    "indirect verifier function designator",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''    case MINIC_EXPRESSION_DEREFERENCE:\n        return minic_riscv64_emit_expression(\n                   file, program, function, expression->value.unary.operand) &&\n               minic_riscv64_emit_scalar_load(file, expression->type, "a0", "a0");\n''',
    '''    case MINIC_EXPRESSION_DEREFERENCE:\n        if (minic_type_is_function(expression->type)) {\n            return minic_riscv64_emit_expression(\n                file, program, function, expression->value.unary.operand);\n        }\n        return minic_riscv64_emit_expression(\n                   file, program, function, expression->value.unary.operand) &&\n               minic_riscv64_emit_scalar_load(file, expression->type, "a0", "a0");\n''',
    "RV64 function designator dereference",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''            indirect_callee = minic_c0_program_expression(program, expression->value.call.callee);\n            if (indirect_callee == NULL ||\n                !minic_type_pointee(indirect_callee->type, &function_type) ||\n                !minic_type_is_function(function_type)) {\n                return false;\n            }\n''',
    '''            indirect_callee = minic_c0_program_expression(program, expression->value.call.callee);\n            if (indirect_callee == NULL) {\n                return false;\n            }\n            function_type = indirect_callee->type;\n            if (!minic_type_is_function(function_type) &&\n                (!minic_type_pointee(indirect_callee->type, &function_type) ||\n                 !minic_type_is_function(function_type))) {\n                return false;\n            }\n''',
    "RV64 indirect function designator type",
)

print("staged indirect calls through function designators such as (*fp)(...)")
