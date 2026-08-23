#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:180]!r}")
    p.write_text(text.replace(old, new, 1))


# Core IR owns the semantic operation; target feature selection stays in the backend.
replace_once(
    "src/core/core_ir.h",
    "    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT,\n    MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO,\n",
    "    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT,\n"
    "    MINIC_CORE_INSTRUCTION_INTEGER_COUNT_LEADING_ZEROS,\n"
    "    MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO,\n",
)

replace_once(
    "src/core/core_ir.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_is_integer(instruction->type) &&\n"
    "               instruction->value.operand < function->value_count &&\n"
    "               available_values[instruction->value.operand] &&\n"
    "               minic_type_equal(function->values[instruction->value.operand].type,\n"
    "                                instruction->type);\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_is_integer(instruction->type) &&\n"
    "               instruction->value.operand < function->value_count &&\n"
    "               available_values[instruction->value.operand] &&\n"
    "               minic_type_equal(function->values[instruction->value.operand].type,\n"
    "                                instruction->type);\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_COUNT_LEADING_ZEROS:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_equal(instruction->type, minic_type_int()) &&\n"
    "               instruction->value.operand < function->value_count &&\n"
    "               available_values[instruction->value.operand] &&\n"
    "               minic_type_is_integer(function->values[instruction->value.operand].type) &&\n"
    "               !minic_type_is_bool_integer(function->values[instruction->value.operand].type);\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n",
)

replace_once(
    "src/core/core_ir.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = inot %%%\" PRIu32 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.operand) >= 0;\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = inot %%%\" PRIu32 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.operand) >= 0;\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_COUNT_LEADING_ZEROS:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = count.leading.zeros %%%\" PRIu32 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.operand) >= 0;\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n",
)

# The GNU builtin is already normalized by the frontend to unsigned long long -> int.
# Lower only that semantic builtin here; other unary builtins remain fail-closed.
replace_once(
    "src/core/core_lower.c",
    "    if (expression->kind == MINIC_EXPRESSION_UNARY &&\n"
    "        (expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||\n",
    "    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&\n"
    "        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZLL) {\n"
    "        const MinicExpression *operand;\n"
    "        MinicCoreInstruction count_instruction;\n"
    "        MinicCoreValueId operand_value;\n"
    "        MinicCoreLowerStatus status;\n"
    "\n"
    "        operand = minic_c0_program_expression(\n"
    "            context->body->program, expression->value.builtin_unary.operand);\n"
    "        if (operand == NULL ||\n"
    "            !minic_type_equal(expression->type, minic_type_int()) ||\n"
    "            !minic_type_equal(operand->type, minic_type_unsigned_long_long())) {\n"
    "            return MINIC_CORE_LOWER_UNSUPPORTED;\n"
    "        }\n"
    "        status = lower_expression(\n"
    "            context, expression->value.builtin_unary.operand, &operand_value);\n"
    "        if (status != MINIC_CORE_LOWER_OK) {\n"
    "            return status;\n"
    "        }\n"
    "        (void)memset(&count_instruction, 0, sizeof(count_instruction));\n"
    "        count_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_COUNT_LEADING_ZEROS;\n"
    "        count_instruction.span = expression->span;\n"
    "        count_instruction.type = minic_type_int();\n"
    "        count_instruction.result = MINIC_CORE_VALUE_INVALID;\n"
    "        count_instruction.value.operand = operand_value;\n"
    "        return minic_core_function_append_value_instruction(\n"
    "                   context->function, context->block_id, &count_instruction, value_id)\n"
    "                   ? MINIC_CORE_LOWER_OK\n"
    "                   : MINIC_CORE_LOWER_ERROR;\n"
    "    }\n"
    "    if (expression->kind == MINIC_EXPRESSION_UNARY &&\n"
    "        (expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||\n",
)

# RV64 currently realizes the 64-bit form in baseline RV64I, deliberately avoiding
# a Zbb dependency. This mirrors the already-qualified legacy backend policy.
replace_once(
    "src/target/riscv64/core_codegen.c",
    "static bool core_instruction_supported(const MinicC0Program *program,\n",
    "static bool core_count_leading_zeros_supported(const MinicC0Program *program,\n"
    "                                               const MinicCoreFunction *function,\n"
    "                                               const MinicCoreInstruction *instruction) {\n"
    "    size_t operand_size;\n"
    "    size_t operand_alignment;\n"
    "    MinicCoreValueId operand;\n"
    "\n"
    "    if (program == NULL || function == NULL || instruction == NULL ||\n"
    "        instruction->kind != MINIC_CORE_INSTRUCTION_INTEGER_COUNT_LEADING_ZEROS ||\n"
    "        !minic_type_equal(instruction->type, minic_type_int())) {\n"
    "        return false;\n"
    "    }\n"
    "    operand = instruction->value.operand;\n"
    "    if (operand >= function->value_count ||\n"
    "        !minic_type_is_integer(function->values[operand].type) ||\n"
    "        minic_type_is_bool_integer(function->values[operand].type) ||\n"
    "        !minic_data_layout_type(minic_default_data_layout(),\n"
    "                                program,\n"
    "                                function->values[operand].type,\n"
    "                                &operand_size,\n"
    "                                &operand_alignment)) {\n"
    "        return false;\n"
    "    }\n"
    "    (void)operand_alignment;\n"
    "    return operand_size == 8U;\n"
    "}\n"
    "\n"
    "static bool core_instruction_supported(const MinicC0Program *program,\n",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n",
)

# Add the count operation as a checked target capability instead of the generic true group.
replace_once(
    "src/target/riscv64/core_codegen.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_COUNT_LEADING_ZEROS:\n"
    "        return core_count_leading_zeros_supported(program, function, instruction);\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {\n",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
    "        if (!load_core_value(file, frame, instruction->value.operand, \"t0\") ||\n"
    "            fprintf(file, \"  xori t0, t0, -1\\n\") < 0 ||\n"
    "            !minic_riscv64_emit_integer_conversion_for_program(\n"
    "                file, program, instruction->type, \"t0\")) {\n"
    "            return false;\n"
    "        }\n"
    "        return store_core_value(file, frame, instruction->result, \"t0\");\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
    "        if (!load_core_value(file, frame, instruction->value.operand, \"t0\") ||\n"
    "            fprintf(file, \"  xori t0, t0, -1\\n\") < 0 ||\n"
    "            !minic_riscv64_emit_integer_conversion_for_program(\n"
    "                file, program, instruction->type, \"t0\")) {\n"
    "            return false;\n"
    "        }\n"
    "        return store_core_value(file, frame, instruction->result, \"t0\");\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_COUNT_LEADING_ZEROS:\n"
    "        if (!core_count_leading_zeros_supported(program, function, instruction) ||\n"
    "            !load_core_value(file, frame, instruction->value.operand, \"t0\") ||\n"
    "            fprintf(file,\n"
    "                    \"  li t1, 0\\n\"\n"
    "                    \"  srli t2, t0, 32\\n\"\n"
    "                    \"  bnez t2, 1f\\n\"\n"
    "                    \"  addi t1, t1, 32\\n\"\n"
    "                    \"  slli t0, t0, 32\\n\"\n"
    "                    \"1:\\n\"\n"
    "                    \"  srli t2, t0, 48\\n\"\n"
    "                    \"  bnez t2, 2f\\n\"\n"
    "                    \"  addi t1, t1, 16\\n\"\n"
    "                    \"  slli t0, t0, 16\\n\"\n"
    "                    \"2:\\n\"\n"
    "                    \"  srli t2, t0, 56\\n\"\n"
    "                    \"  bnez t2, 3f\\n\"\n"
    "                    \"  addi t1, t1, 8\\n\"\n"
    "                    \"  slli t0, t0, 8\\n\"\n"
    "                    \"3:\\n\"\n"
    "                    \"  srli t2, t0, 60\\n\"\n"
    "                    \"  bnez t2, 4f\\n\"\n"
    "                    \"  addi t1, t1, 4\\n\"\n"
    "                    \"  slli t0, t0, 4\\n\"\n"
    "                    \"4:\\n\"\n"
    "                    \"  srli t2, t0, 62\\n\"\n"
    "                    \"  bnez t2, 5f\\n\"\n"
    "                    \"  addi t1, t1, 2\\n\"\n"
    "                    \"  slli t0, t0, 2\\n\"\n"
    "                    \"5:\\n\"\n"
    "                    \"  srli t2, t0, 63\\n\"\n"
    "                    \"  bnez t2, 6f\\n\"\n"
    "                    \"  addi t1, t1, 1\\n\"\n"
    "                    \"6:\\n\") < 0 ||\n"
    "            !store_core_value(file, frame, instruction->result, \"t1\")) {\n"
    "            return false;\n"
    "        }\n"
    "        return true;\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n",
)

print("M36_PATCH_APPLIED")
