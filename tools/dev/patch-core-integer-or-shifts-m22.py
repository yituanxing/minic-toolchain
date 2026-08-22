#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    file.write_text(text.replace(old, new, 1))


replace_once(
    "src/core/core_ir.h",
    """    MINIC_CORE_INSTRUCTION_INTEGER_ADD,\n    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND,\n    MINIC_CORE_INSTRUCTION_SCALAR_EQUAL,\n""",
    """    MINIC_CORE_INSTRUCTION_INTEGER_ADD,\n    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND,\n    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR,\n    MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT,\n    MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT,\n    MINIC_CORE_INSTRUCTION_SCALAR_EQUAL,\n""",
    "Core instruction enum",
)

replace_once(
    "src/core/core_ir.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n        if (!instruction_result_is_valid(function, instruction) ||\n            !minic_type_is_integer(instruction->type) ||\n            instruction->value.binary.left >= function->value_count ||\n            instruction->value.binary.right >= function->value_count ||\n            !available_values[instruction->value.binary.left] ||\n            !available_values[instruction->value.binary.right]) {\n            return false;\n        }\n        left = &function->values[instruction->value.binary.left];\n        right = &function->values[instruction->value.binary.right];\n        return minic_type_equal(left->type, instruction->type) &&\n               minic_type_equal(right->type, instruction->type);\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:\n        if (!instruction_result_is_valid(function, instruction) ||\n            !minic_type_is_integer(instruction->type) ||\n            instruction->value.binary.left >= function->value_count ||\n            instruction->value.binary.right >= function->value_count ||\n            !available_values[instruction->value.binary.left] ||\n            !available_values[instruction->value.binary.right]) {\n            return false;\n        }\n        left = &function->values[instruction->value.binary.left];\n        right = &function->values[instruction->value.binary.right];\n        return minic_type_equal(left->type, instruction->type) &&\n               minic_type_equal(right->type, instruction->type);\n    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:\n    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:\n        if (!instruction_result_is_valid(function, instruction) ||\n            !minic_type_is_integer(instruction->type) ||\n            instruction->value.binary.left >= function->value_count ||\n            instruction->value.binary.right >= function->value_count ||\n            !available_values[instruction->value.binary.left] ||\n            !available_values[instruction->value.binary.right]) {\n            return false;\n        }\n        left = &function->values[instruction->value.binary.left];\n        right = &function->values[instruction->value.binary.right];\n        return minic_type_equal(left->type, instruction->type) &&\n               minic_type_is_integer(right->type);\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    "Core verifier integer binary ops",
)

replace_once(
    "src/core/core_ir.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = and.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = and.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = or.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = shl.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = shr.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    "Core dump integer ops",
)

lower_insert = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreValueId left;
        MinicCoreValueId left_source;
        MinicCoreValueId right;
        MinicCoreValueId right_source;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_expression(context, expression->value.binary.left, &left_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = append_integer_conversion(
            context, left_expression->span, expression->type, left_source, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = append_integer_conversion(
            context, right_expression->span, expression->type, right_source, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreValueId left;
        MinicCoreValueId left_source;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !minic_type_is_integer(left_expression->type) ||
            !minic_type_is_integer(right_expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.binary.left, &left_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = append_integer_conversion(
            context, left_expression->span, expression->type, left_source, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (left >= context->function->value_count || right >= context->function->value_count ||
            !minic_type_equal(context->function->values[left].type, expression->type) ||
            !minic_type_is_integer(context->function->values[right].type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.kind = expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT
                               ? MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT
                               : MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
'''
replace_once(
    "src/core/core_lower.c",
    """    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&\n        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {\n""",
    lower_insert + """    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&\n        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {\n""",
    "Core lowering insertion",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\n    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\n    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:\n    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:\n    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    "RV64 Core support switch",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  and t0, t0, t1\\n\") < 0 ||\n            !minic_riscv64_emit_integer_conversion_for_program(\n                file, program, instruction->type, \"t0\")) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  and t0, t0, t1\\n\") < 0 ||\n            !minic_riscv64_emit_integer_conversion_for_program(\n                file, program, instruction->type, \"t0\")) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  or t0, t0, t1\\n\") < 0 ||\n            !minic_riscv64_emit_integer_conversion_for_program(\n                file, program, instruction->type, \"t0\")) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  sll t0, t0, t1\\n\") < 0 ||\n            !minic_riscv64_emit_integer_conversion_for_program(\n                file, program, instruction->type, \"t0\")) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT: {\n        MinicType effective_type;\n        const char *opcode;\n\n        if (!minic_c0_type_effective_integer_type(program, instruction->type, &effective_type)) {\n            return false;\n        }\n        opcode = minic_type_is_unsigned_integer(effective_type) ? \"srl\" : \"sra\";\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  %s t0, t0, t1\\n\", opcode) < 0 ||\n            !minic_riscv64_emit_integer_conversion_for_program(\n                file, program, instruction->type, \"t0\")) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    }\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    "RV64 Core integer op emission",
)

Path("tests/compiler/c0/core_integer_or_shifts_m22.c").write_text(r'''typedef unsigned short core_m22_u16;

unsigned int core_m22_or(unsigned int left, unsigned int right) {
    return left | right;
}

unsigned int core_m22_shift_left(unsigned int value, unsigned int count) {
    return value << count;
}

unsigned int core_m22_shift_right_unsigned(unsigned int value, unsigned int count) {
    return value >> count;
}

int core_m22_shift_right_signed(int value, unsigned int count) {
    return value >> count;
}

core_m22_u16 core_m22_fswab16(core_m22_u16 val) {
    return (core_m22_u16)((((core_m22_u16)(val) & (core_m22_u16)0x00ffU) << 8) |
                          (((core_m22_u16)(val) & (core_m22_u16)0xff00U) >> 8));
}
''')

Path("tests/compiler/c0/core_integer_or_shifts_m22_runtime.c").write_text(r'''#include <stdio.h>

typedef unsigned short core_m22_u16;

unsigned int core_m22_or(unsigned int left, unsigned int right);
unsigned int core_m22_shift_left(unsigned int value, unsigned int count);
unsigned int core_m22_shift_right_unsigned(unsigned int value, unsigned int count);
int core_m22_shift_right_signed(int value, unsigned int count);
core_m22_u16 core_m22_fswab16(core_m22_u16 val);

int main(void) {
    printf("or=%u\n", core_m22_or(0x1200U, 0x0034U));
    printf("shl=%u\n", core_m22_shift_left(0x12U, 8U));
    printf("shru=%u\n", core_m22_shift_right_unsigned(0x123400U, 8U));
    printf("shrs=%d\n", core_m22_shift_right_signed(-256, 8U));
    printf("swab=%u,%u,%u\n",
           (unsigned int)core_m22_fswab16((core_m22_u16)0x1234U),
           (unsigned int)core_m22_fswab16((core_m22_u16)0x00ffU),
           (unsigned int)core_m22_fswab16((core_m22_u16)0xa500U));
    return 0;
}
''')

Path("tests/compiler/c0/run-core-integer-or-shifts-m22.sh").write_text(r'''#!/bin/sh
set -eu

: "${MINIC:?MINIC is required}"
: "${BUILD_DIR:?BUILD_DIR is required}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${HOST_CC:=cc}"

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
source_file="$root_dir/tests/compiler/c0/core_integer_or_shifts_m22.c"
runtime_file="$root_dir/tests/compiler/c0/core_integer_or_shifts_m22_runtime.c"
mkdir -p "$BUILD_DIR"

"$HOST_CC" -E -P -std=gnu11 "$source_file" -o "$BUILD_DIR/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$BUILD_DIR/input.i" -o "$BUILD_DIR/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$BUILD_DIR/input.i" -o "$BUILD_DIR/core.s"

grep -F 'or t0, t0, t1' "$BUILD_DIR/core.s" >/dev/null
grep -F 'sll t0, t0, t1' "$BUILD_DIR/core.s" >/dev/null
grep -F 'srl t0, t0, t1' "$BUILD_DIR/core.s" >/dev/null
grep -F 'sra t0, t0, t1' "$BUILD_DIR/core.s" >/dev/null

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$BUILD_DIR/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$BUILD_DIR/core.s" -o "$BUILD_DIR/minic-rv64"
"$QEMU_RISCV64" "$BUILD_DIR/reference-rv64" > "$BUILD_DIR/reference.out"
"$QEMU_RISCV64" "$BUILD_DIR/minic-rv64" > "$BUILD_DIR/minic.out"
diff -u "$BUILD_DIR/reference.out" "$BUILD_DIR/minic.out"
grep -Fx 'or=4660' "$BUILD_DIR/minic.out" >/dev/null
grep -Fx 'shl=4608' "$BUILD_DIR/minic.out" >/dev/null
grep -Fx 'shru=4660' "$BUILD_DIR/minic.out" >/dev/null
grep -Fx 'shrs=-1' "$BUILD_DIR/minic.out" >/dev/null
grep -Fx 'swab=13330,65280,165' "$BUILD_DIR/minic.out" >/dev/null
printf 'PASS compiler/c0/core-integer-or-shifts-m22\n'
''')

print("staged M22 Core integer OR and shifts")
