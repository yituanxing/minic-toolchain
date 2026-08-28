#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


# Batch H: pointer relational comparisons are semantically pointer operations,
# not integer arithmetic. Give Core a target-neutral POINTER_LESS primitive;
# targets decide how pointer ordering is spelled. <=, > and >= continue to be
# composed from less-than plus scalar-is-zero, just like integer relations.
replace_once(
    "src/core/core_ir.h",
    """    MINIC_CORE_INSTRUCTION_INTEGER_LESS,\n    MINIC_CORE_INSTRUCTION_SCALAR_EQUAL,\n""",
    """    MINIC_CORE_INSTRUCTION_INTEGER_LESS,\n    MINIC_CORE_INSTRUCTION_POINTER_LESS,\n    MINIC_CORE_INSTRUCTION_SCALAR_EQUAL,\n""",
)

replace_once(
    "src/core/core_ir.c",
    """        return minic_type_is_integer(left->type) && minic_type_equal(left->type, right->type);\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    """        return minic_type_is_integer(left->type) && minic_type_equal(left->type, right->type);\n    case MINIC_CORE_INSTRUCTION_POINTER_LESS:\n        if (!instruction_result_is_valid(function, instruction) ||\n            !minic_type_equal(instruction->type, minic_type_int()) ||\n            instruction->value.binary.left >= function->value_count ||\n            instruction->value.binary.right >= function->value_count ||\n            !available_values[instruction->value.binary.left] ||\n            !available_values[instruction->value.binary.right]) {\n            return false;\n        }\n        left = &function->values[instruction->value.binary.left];\n        right = &function->values[instruction->value.binary.right];\n        return minic_type_is_pointer(left->type) && minic_type_equal(left->type, right->type);\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
)

replace_once(
    "src/core/core_ir.c",
    """    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = eq.scalar %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n""",
    """    case MINIC_CORE_INSTRUCTION_POINTER_LESS:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = lt.ptr %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = eq.scalar %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n""",
)

# Insert pointer relations before the integer-only relational paths.
anchor = """    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        expression->value.binary.operator_kind == MINIC_BINARY_LESS) {\n"""
block = """    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        (expression->value.binary.operator_kind == MINIC_BINARY_LESS ||\n         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||\n         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||\n         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {\n        const MinicExpression *left_expression;\n        const MinicExpression *right_expression;\n        MinicCoreInstruction invert_instruction;\n        MinicCoreValueId left;\n        MinicCoreValueId less_value;\n        MinicCoreValueId right;\n        MinicCoreLowerStatus status;\n        MinicType common_type;\n        MinicType left_type;\n        MinicType right_type;\n        bool invert;\n        bool swap;\n\n        if (!minic_type_equal(expression->type, minic_type_int())) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        left_expression =\n            minic_c0_program_expression(context->body->program, expression->value.binary.left);\n        right_expression =\n            minic_c0_program_expression(context->body->program, expression->value.binary.right);\n        if (left_expression != NULL && right_expression != NULL &&\n            core_scalar_expression_value_type(context->body, left_expression, &left_type) &&\n            core_scalar_expression_value_type(context->body, right_expression, &right_type) &&\n            minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type)) {\n            if (!minic_c0_pointer_relational_compatible(\n                    context->body->program, left_type, right_type) ||\n                !minic_type_conditional_pointer_common(left_type, right_type, &common_type)) {\n                return MINIC_CORE_LOWER_UNSUPPORTED;\n            }\n            status = lower_expression(context, expression->value.binary.left, &left);\n            if (status != MINIC_CORE_LOWER_OK) {\n                return status;\n            }\n            if (left >= context->function->value_count) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            if (!minic_type_equal(context->function->values[left].type, common_type)) {\n                status = append_scalar_bitcast(\n                    context, left_expression->span, common_type, left, &left);\n                if (status != MINIC_CORE_LOWER_OK) {\n                    return status;\n                }\n            }\n            status = lower_expression(context, expression->value.binary.right, &right);\n            if (status != MINIC_CORE_LOWER_OK) {\n                return status;\n            }\n            if (right >= context->function->value_count) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            if (!minic_type_equal(context->function->values[right].type, common_type)) {\n                status = append_scalar_bitcast(\n                    context, right_expression->span, common_type, right, &right);\n                if (status != MINIC_CORE_LOWER_OK) {\n                    return status;\n                }\n            }\n            swap = expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||\n                   expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL;\n            invert = expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||\n                     expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL;\n            (void)memset(&instruction, 0, sizeof(instruction));\n            instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_LESS;\n            instruction.span = expression->span;\n            instruction.type = minic_type_int();\n            instruction.result = MINIC_CORE_VALUE_INVALID;\n            instruction.value.binary.left = swap ? right : left;\n            instruction.value.binary.right = swap ? left : right;\n            if (!minic_core_function_append_value_instruction(\n                    context->function, context->block_id, &instruction, &less_value)) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            if (!invert) {\n                *value_id = less_value;\n                return MINIC_CORE_LOWER_OK;\n            }\n            (void)memset(&invert_instruction, 0, sizeof(invert_instruction));\n            invert_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;\n            invert_instruction.span = expression->span;\n            invert_instruction.type = minic_type_int();\n            invert_instruction.result = MINIC_CORE_VALUE_INVALID;\n            invert_instruction.value.operand = less_value;\n            return minic_core_function_append_value_instruction(\n                       context->function, context->block_id, &invert_instruction, value_id)\n                       ? MINIC_CORE_LOWER_OK\n                       : MINIC_CORE_LOWER_ERROR;\n        }\n    }\n\n"""
replace_once("src/core/core_lower.c", anchor, block + anchor)

# The transitional frontend can represent an outer legacy array dimension via
# field/local array metadata while its element type is itself a materialized
# array type. M71 rejected that valid nested shape merely because base->type is
# an array. lower_address(base) already yields exactly pointer-to-element here,
# so keep the real type/value checks and remove only that obsolete scalar-only
# assumption. This is the generic 2-D (and deeper) legacy-array subscript seam.
replace_once(
    "src/core/core_lower.c",
    """            } else if (minic_type_is_array(base->type) ||\n                       !minic_type_equal(base->type, array_info.element_type) ||\n                       !minic_type_equal(context->function->values[base_value].type,\n                                         pointer_type)) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n""",
    """            } else if (!minic_type_equal(base->type, array_info.element_type) ||\n                       !minic_type_equal(context->function->values[base_value].type,\n                                         pointer_type)) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n""",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:\n    case MINIC_CORE_INSTRUCTION_POINTER_LESS:\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n""",
    """    case MINIC_CORE_INSTRUCTION_POINTER_LESS:\n        if (instruction->value.binary.left >= function->value_count ||\n            instruction->value.binary.right >= function->value_count ||\n            !minic_type_is_pointer(function->values[instruction->value.binary.left].type) ||\n            !minic_type_equal(function->values[instruction->value.binary.left].type,\n                              function->values[instruction->value.binary.right].type) ||\n            !load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  sltu t0, t0, t1\\n\") < 0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n""",
)

print("CORE_BATCH_H_PATCHED pointer relational + nested legacy array subscript")
