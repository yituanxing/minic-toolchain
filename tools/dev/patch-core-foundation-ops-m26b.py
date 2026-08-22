#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


def insert_before(path: str, marker: str, insertion: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(marker)
    if count != 1:
        raise SystemExit(f"{path}: expected one marker, found {count}")
    p.write_text(text.replace(marker, insertion + marker, 1))


replace_once(
    "src/core/core_ir.h",
    """    MINIC_CORE_INSTRUCTION_INTEGER_ADD,\n    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND,\n    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR,\n""",
    """    MINIC_CORE_INSTRUCTION_INTEGER_ADD,\n    MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT,\n    MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY,\n    MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE,\n    MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER,\n    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND,\n    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR,\n    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR,\n""",
)

replace_once(
    "src/core/core_ir.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:\n    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:\n    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:\n    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:\n""",
)

insert_before(
    "src/core/core_ir.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n        return fprintf(output,\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = sub.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = mul.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = div.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = rem.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n""",
)
insert_before(
    "src/core/core_ir.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:\n        return fprintf(output,\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = xor.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n""",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:\n    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:\n    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:\n    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:\n""",
)

insert_before(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  sub t0, t0, t1\\n\") < 0 ||\n            !minic_riscv64_emit_integer_conversion_for_program(\n                file, program, instruction->type, \"t0\")) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  mul t0, t0, t1\\n\") < 0 ||\n            !minic_riscv64_emit_integer_conversion_for_program(\n                file, program, instruction->type, \"t0\")) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:\n    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {\n        MinicType effective_type;\n        const char *opcode;\n\n        if (!minic_c0_type_effective_integer_type(program, instruction->type, &effective_type)) {\n            return false;\n        }\n        if (instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE) {\n            opcode = minic_type_is_unsigned_integer(effective_type) ? \"divu\" : \"div\";\n        } else {\n            opcode = minic_type_is_unsigned_integer(effective_type) ? \"remu\" : \"rem\";\n        }\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  %s t0, t0, t1\\n\", opcode) < 0 ||\n            !minic_riscv64_emit_integer_conversion_for_program(\n                file, program, instruction->type, \"t0\")) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    }\n""",
)
insert_before(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  xor t0, t0, t1\\n\") < 0 ||\n            !minic_riscv64_emit_integer_conversion_for_program(\n                file, program, instruction->type, \"t0\")) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n""",
)

insert_before(
    "src/core/core_lower.c",
    """    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        expression->value.binary.operator_kind == MINIC_BINARY_ADD &&\n        minic_type_is_pointer(expression->type)) {\n""",
    """    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        (expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||\n         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||\n         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {\n        const MinicExpression *left_expression;\n        const MinicExpression *right_expression;\n        MinicCoreInstruction invert_instruction;\n        MinicCoreValueId left;\n        MinicCoreValueId less_value;\n        MinicCoreValueId right;\n        MinicCoreLowerStatus status;\n        MinicType common_type;\n        MinicType left_type;\n        MinicType right_type;\n        bool invert;\n        bool swap;\n\n        if (!minic_type_equal(expression->type, minic_type_int()) || context->target == NULL) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        left_expression =\n            minic_c0_program_expression(context->body->program, expression->value.binary.left);\n        right_expression =\n            minic_c0_program_expression(context->body->program, expression->value.binary.right);\n        if (left_expression == NULL || right_expression == NULL ||\n            !core_scalar_expression_value_type(context->body, left_expression, &left_type) ||\n            !core_scalar_expression_value_type(context->body, right_expression, &right_type) ||\n            !minic_type_is_integer(left_type) || !minic_type_is_integer(right_type) ||\n            !minic_target_info_integer_common_for_program(context->target,\n                                                          context->body->program,\n                                                          left_type,\n                                                          right_type,\n                                                          &common_type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        status = lower_integer_binary_operands(context,\n                                               expression->value.binary.left,\n                                               expression->value.binary.right,\n                                               common_type,\n                                               &left,\n                                               &right);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        swap = expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||\n               expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL;\n        invert = expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||\n                 expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL;\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;\n        instruction.span = expression->span;\n        instruction.type = minic_type_int();\n        instruction.result = MINIC_CORE_VALUE_INVALID;\n        instruction.value.binary.left = swap ? right : left;\n        instruction.value.binary.right = swap ? left : right;\n        if (!minic_core_function_append_value_instruction(\n                context->function, context->block_id, &instruction, &less_value)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        if (!invert) {\n            *value_id = less_value;\n            return MINIC_CORE_LOWER_OK;\n        }\n        (void)memset(&invert_instruction, 0, sizeof(invert_instruction));\n        invert_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;\n        invert_instruction.span = expression->span;\n        invert_instruction.type = minic_type_int();\n        invert_instruction.result = MINIC_CORE_VALUE_INVALID;\n        invert_instruction.value.operand = less_value;\n        return minic_core_function_append_value_instruction(\n                   context->function, context->block_id, &invert_instruction, value_id)\n                   ? MINIC_CORE_LOWER_OK\n                   : MINIC_CORE_LOWER_ERROR;\n    }\n""",
)

insert_before(
    "src/core/core_lower.c",
    """    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {\n""",
    """    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||\n         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||\n         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE ||\n         expression->value.binary.operator_kind == MINIC_BINARY_REMAINDER ||\n         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_XOR)) {\n        MinicCoreValueId left;\n        MinicCoreValueId right;\n        MinicCoreLowerStatus status;\n\n        if (!minic_type_is_integer(expression->type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        status = lower_integer_binary_operands(context,\n                                               expression->value.binary.left,\n                                               expression->value.binary.right,\n                                               expression->type,\n                                               &left,\n                                               &right);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        switch (expression->value.binary.operator_kind) {\n        case MINIC_BINARY_SUBTRACT:\n            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;\n            break;\n        case MINIC_BINARY_MULTIPLY:\n            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY;\n            break;\n        case MINIC_BINARY_DIVIDE:\n            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE;\n            break;\n        case MINIC_BINARY_REMAINDER:\n            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER;\n            break;\n        case MINIC_BINARY_BITWISE_XOR:\n            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR;\n            break;\n        default:\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        instruction.value.binary.left = left;\n        instruction.value.binary.right = right;\n        return minic_core_function_append_value_instruction(\n                   context->function, context->block_id, &instruction, value_id)\n                   ? MINIC_CORE_LOWER_OK\n                   : MINIC_CORE_LOWER_ERROR;\n    }\n""",
)
