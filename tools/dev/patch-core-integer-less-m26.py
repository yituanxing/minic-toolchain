#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/core/core_ir.h",
    """    MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT,\n    MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT,\n    MINIC_CORE_INSTRUCTION_SCALAR_EQUAL,\n""",
    """    MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT,\n    MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT,\n    MINIC_CORE_INSTRUCTION_INTEGER_LESS,\n    MINIC_CORE_INSTRUCTION_SCALAR_EQUAL,\n""",
)

replace_once(
    "src/core/core_ir.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:\n    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:\n        if (!instruction_result_is_valid(function, instruction) ||\n            !minic_type_is_integer(instruction->type) ||\n            instruction->value.binary.left >= function->value_count ||\n            instruction->value.binary.right >= function->value_count ||\n            !available_values[instruction->value.binary.left] ||\n            !available_values[instruction->value.binary.right]) {\n            return false;\n        }\n        left = &function->values[instruction->value.binary.left];\n        right = &function->values[instruction->value.binary.right];\n        return minic_type_equal(left->type, instruction->type) &&\n               minic_type_is_integer(right->type);\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:\n    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:\n        if (!instruction_result_is_valid(function, instruction) ||\n            !minic_type_is_integer(instruction->type) ||\n            instruction->value.binary.left >= function->value_count ||\n            instruction->value.binary.right >= function->value_count ||\n            !available_values[instruction->value.binary.left] ||\n            !available_values[instruction->value.binary.right]) {\n            return false;\n        }\n        left = &function->values[instruction->value.binary.left];\n        right = &function->values[instruction->value.binary.right];\n        return minic_type_equal(left->type, instruction->type) &&\n               minic_type_is_integer(right->type);\n    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:\n        if (!instruction_result_is_valid(function, instruction) ||\n            !minic_type_equal(instruction->type, minic_type_int()) ||\n            instruction->value.binary.left >= function->value_count ||\n            instruction->value.binary.right >= function->value_count ||\n            !available_values[instruction->value.binary.left] ||\n            !available_values[instruction->value.binary.right]) {\n            return false;\n        }\n        left = &function->values[instruction->value.binary.left];\n        right = &function->values[instruction->value.binary.right];\n        return minic_type_is_integer(left->type) && minic_type_equal(left->type, right->type);\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
)

replace_once(
    "src/core/core_ir.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = shr.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = shr.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = lt.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
)

replace_once(
    "src/core/core_lower.c",
    """    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        expression->value.binary.operator_kind == MINIC_BINARY_ADD &&\n        minic_type_is_pointer(expression->type)) {\n""",
    """    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        expression->value.binary.operator_kind == MINIC_BINARY_LESS) {\n        const MinicExpression *left_expression;\n        const MinicExpression *right_expression;\n        MinicCoreValueId left;\n        MinicCoreValueId right;\n        MinicCoreLowerStatus status;\n        MinicType common_type;\n\n        if (!minic_type_equal(expression->type, minic_type_int()) || context->target == NULL) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        left_expression =\n            minic_c0_program_expression(context->body->program, expression->value.binary.left);\n        right_expression =\n            minic_c0_program_expression(context->body->program, expression->value.binary.right);\n        if (left_expression == NULL || right_expression == NULL ||\n            !minic_type_is_integer(left_expression->type) ||\n            !minic_type_is_integer(right_expression->type) ||\n            !minic_target_info_integer_common_for_program(context->target,\n                                                          context->body->program,\n                                                          left_expression->type,\n                                                          right_expression->type,\n                                                          &common_type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        status = lower_integer_binary_operands(context,\n                                               expression->value.binary.left,\n                                               expression->value.binary.right,\n                                               common_type,\n                                               &left,\n                                               &right);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;\n        instruction.span = expression->span;\n        instruction.type = minic_type_int();\n        instruction.result = MINIC_CORE_VALUE_INVALID;\n        instruction.value.binary.left = left;\n        instruction.value.binary.right = right;\n        return minic_core_function_append_value_instruction(\n                   context->function, context->block_id, &instruction, value_id)\n                   ? MINIC_CORE_LOWER_OK\n                   : MINIC_CORE_LOWER_ERROR;\n    }\n    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        expression->value.binary.operator_kind == MINIC_BINARY_ADD &&\n        minic_type_is_pointer(expression->type)) {\n""",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:\n    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:\n    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:\n    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  xor t0, t0, t1\\n  seqz t0, t0\\n\") < 0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_LESS: {\n        MinicType effective_type;\n        MinicType operand_type;\n        const char *opcode;\n\n        if (instruction->value.binary.left >= function->value_count ||\n            instruction->value.binary.right >= function->value_count) {\n            return false;\n        }\n        operand_type = function->values[instruction->value.binary.left].type;\n        if (!minic_type_is_integer(operand_type) ||\n            !minic_type_equal(operand_type,\n                              function->values[instruction->value.binary.right].type) ||\n            !minic_c0_type_effective_integer_type(program, operand_type, &effective_type)) {\n            return false;\n        }\n        opcode = minic_type_is_unsigned_integer(effective_type) ? \"sltu\" : \"slt\";\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  %s t0, t0, t1\\n\", opcode) < 0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    }\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  xor t0, t0, t1\\n  seqz t0, t0\\n\") < 0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n""",
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    """core_integer_binary_preservation_m25b_focused() {\n    MINIC=\"$root/build/ci-debug/bin/minic\" BUILD_DIR=\"$root/build/ci-core-integer-binary-preservation-m25b\" RISCV_CC=riscv64-linux-gnu-gcc QEMU_RISCV64=qemu-riscv64 sh tests/compiler/c0/run-core-integer-binary-preservation-m25b.sh\n}\n\nruntime_record_fam_prefix_focused() {\n""",
    """core_integer_binary_preservation_m25b_focused() {\n    MINIC=\"$root/build/ci-debug/bin/minic\" BUILD_DIR=\"$root/build/ci-core-integer-binary-preservation-m25b\" RISCV_CC=riscv64-linux-gnu-gcc QEMU_RISCV64=qemu-riscv64 sh tests/compiler/c0/run-core-integer-binary-preservation-m25b.sh\n}\n\ncore_integer_less_m26_focused() {\n    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n    BUILD_DIR=\"$root/build/ci-core-integer-less-m26\" \\\n    RISCV_CC=riscv64-linux-gnu-gcc \\\n    QEMU_RISCV64=qemu-riscv64 \\\n        sh tests/compiler/c0/run-core-integer-less-m26.sh\n}\n\nruntime_record_fam_prefix_focused() {\n""",
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    """start_gate core-discard-expression-m25-focused core_discard_expression_m25_focused\nstart_gate core-integer-binary-preservation-m25b-focused core_integer_binary_preservation_m25b_focused\nstart_gate record-fam-prefix-focused runtime_record_fam_prefix_focused\n""",
    """start_gate core-discard-expression-m25-focused core_discard_expression_m25_focused\nstart_gate core-integer-binary-preservation-m25b-focused core_integer_binary_preservation_m25b_focused\nstart_gate core-integer-less-m26-focused core_integer_less_m26_focused\nstart_gate record-fam-prefix-focused runtime_record_fam_prefix_focused\n""",
)

Path("tests/compiler/c0/core_integer_less_m26.c").write_text(
    """int core_m26_signed_less(long left, long right) {\n    return left < right;\n}\n\nint core_m26_unsigned_less(unsigned long left, unsigned long right) {\n    return left < right;\n}\n\nint core_m26_mixed_less(unsigned int left, unsigned long right) {\n    return left < right;\n}\n\nvoid core_m26_array_loop(unsigned int *dst, const unsigned int *src, unsigned long len) {\n    unsigned long i;\n\n    for (i = 0; i < len; i++) {\n        dst[i] = src[i];\n    }\n}\n"""
)

Path("tests/compiler/c0/core_integer_less_m26_runtime.c").write_text(
    """int core_m26_signed_less(long left, long right);\nint core_m26_unsigned_less(unsigned long left, unsigned long right);\nint core_m26_mixed_less(unsigned int left, unsigned long right);\nvoid core_m26_array_loop(unsigned int *dst, const unsigned int *src, unsigned long len);\n\nint main(void) {\n    unsigned int src[4] = {11U, 22U, 33U, 44U};\n    unsigned int dst[4] = {0U, 0U, 0U, 0U};\n\n    if (!core_m26_signed_less(-7L, 3L) || core_m26_signed_less(9L, -2L)) {\n        return 1;\n    }\n    if (!core_m26_unsigned_less(3UL, 9UL) || core_m26_unsigned_less(~0UL, 1UL)) {\n        return 2;\n    }\n    if (!core_m26_mixed_less(7U, 0x100000000UL) || core_m26_mixed_less(9U, 2UL)) {\n        return 3;\n    }\n    core_m26_array_loop(dst, src, 4UL);\n    if (dst[0] != 11U || dst[1] != 22U || dst[2] != 33U || dst[3] != 44U) {\n        return 4;\n    }\n    return 0;\n}\n"""
)

Path("tests/compiler/c0/run-core-integer-less-m26.sh").write_text(
    """#!/bin/sh\nset -eu\n: \"${MINIC:?set MINIC}\"\n: \"${RISCV_CC:=riscv64-linux-gnu-gcc}\"\n: \"${QEMU_RISCV64:=qemu-riscv64}\"\n: \"${BUILD_DIR:=build/core-integer-less-m26}\"\nmkdir -p \"$BUILD_DIR\"\nMINIC_CORE_IR=strict \"$MINIC\" -S tests/compiler/c0/core_integer_less_m26.c -o \"$BUILD_DIR/minic.s\"\n\"$RISCV_CC\" -O0 -static tests/compiler/c0/core_integer_less_m26_runtime.c \"$BUILD_DIR/minic.s\" -o \"$BUILD_DIR/minic.elf\"\n\"$QEMU_RISCV64\" \"$BUILD_DIR/minic.elf\"\n\"$RISCV_CC\" -O0 -static tests/compiler/c0/core_integer_less_m26_runtime.c tests/compiler/c0/core_integer_less_m26.c -o \"$BUILD_DIR/gcc.elf\"\n\"$QEMU_RISCV64\" \"$BUILD_DIR/gcc.elf\"\nprintf '%s\\n' 'PASS compiler/c0/core-integer-less-m26'\n"""
)
Path("tests/compiler/c0/run-core-integer-less-m26.sh").chmod(0o755)
