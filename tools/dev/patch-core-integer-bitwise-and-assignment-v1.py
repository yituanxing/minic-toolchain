#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:160]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/core/core_ir.h",
    """    MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT = 0,\n    MINIC_CORE_INSTRUCTION_INTEGER_ADD,\n    MINIC_CORE_INSTRUCTION_SCALAR_EQUAL,\n""",
    """    MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT = 0,\n    MINIC_CORE_INSTRUCTION_INTEGER_ADD,\n    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND,\n    MINIC_CORE_INSTRUCTION_SCALAR_EQUAL,\n""",
)

replace_once(
    "src/core/core_ir.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n        if (!instruction_result_is_valid(function, instruction) ||\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n        if (!instruction_result_is_valid(function, instruction) ||\n""",
)

replace_once(
    "src/core/core_ir.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = add.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = add.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = and.int %%%\" PRIu32 \", %%%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.binary.left,\n                       instruction->value.binary.right) >= 0;\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\n    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\n    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  add t0, t0, t1\\n\") < 0 ||\n            !minic_riscv64_emit_integer_conversion_for_program(\n                file, program, instruction->type, \"t0\")) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
    """    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  add t0, t0, t1\\n\") < 0 ||\n            !minic_riscv64_emit_integer_conversion_for_program(\n                file, program, instruction->type, \"t0\")) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:\n        if (!load_core_value(file, frame, instruction->value.binary.left, \"t0\") ||\n            !load_core_value(file, frame, instruction->value.binary.right, \"t1\") ||\n            fprintf(file, \"  and t0, t0, t1\\n\") < 0 ||\n            !minic_riscv64_emit_integer_conversion_for_program(\n                file, program, instruction->type, \"t0\")) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:\n""",
)

lowering = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {
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
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicCoreValueId address;
        MinicCoreValueId current;
        MinicCoreValueId current_common;
        MinicCoreValueId right;
        MinicCoreValueId right_common;
        MinicCoreValueId result;
        MinicCoreValueId stored_value;
        MinicCoreLowerStatus status;
        MinicType common_type;
        MinicType stored_type;

        target =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        source =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_equal(expression->type, target->type) || minic_type_is_const(target->type) ||
            !minic_type_unqualified(target->type, &stored_type) ||
            !minic_type_is_integer(stored_type) || !minic_type_is_integer(source->type) ||
            !minic_type_integer_common(stored_type, source->type, &common_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_address(context, expression->value.binary.left, &address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
        instruction.span = target->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.load.address = address;
        instruction.value.load.is_volatile = minic_type_is_volatile(target->type);
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &current)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = append_integer_conversion(
            context, target->span, common_type, current, &current_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = append_integer_conversion(context, source->span, common_type, right, &right_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
        instruction.span = expression->span;
        instruction.type = common_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current_common;
        instruction.value.binary.right = right_common;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &result)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = append_integer_conversion(
            context, expression->span, stored_type, result, &stored_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = address;
        instruction.value.store.stored_value = stored_value;
        instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = stored_value;
        return MINIC_CORE_LOWER_OK;
    }
'''
replace_once(
    "src/core/core_lower.c",
    """    if (minic_type_is_integer(expression->type) && context->target != NULL) {\n""",
    lowering + "    if (minic_type_is_integer(expression->type) && context->target != NULL) {\n",
)

replace_once(
    "src/core/core_lower.c",
    """    if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    target_id = expression->value.binary.left;\n""",
    """    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {\n        MinicCoreValueId discarded_value;\n\n        return lower_expression(context, statement->expression, &discarded_value);\n    }\n    if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    target_id = expression->value.binary.left;\n""",
)

Path("tests/compiler/c0/core_integer_bitwise_and_assignment.c").write_text(
    r'''int core_m14_bitwise_and(int left, int right) {
    return left & right;
}

int core_m14_compound_int(int value, int right) {
    value &= right;
    return value;
}

unsigned char core_m14_compound_uchar(unsigned char value, unsigned int right) {
    value &= right;
    return value;
}

_Bool core_m14_compound_bool(_Bool value, _Bool right) {
    value &= right;
    return value;
}

int *core_m14_pick(int *value);
_Bool core_m14_report(int value);

int core_m14_single_lvalue(int *value, int right) {
    return (*core_m14_pick(value) &= right);
}

_Bool core_m14_linux_tail(int value) {
    _Bool ret = 1;
    ret &= core_m14_report(value);
    return ret;
}
'''
)

Path("tests/compiler/c0/core_integer_bitwise_and_assignment_runtime.c").write_text(
    r'''#include <stdio.h>

int core_m14_bitwise_and(int left, int right);
int core_m14_compound_int(int value, int right);
unsigned char core_m14_compound_uchar(unsigned char value, unsigned int right);
_Bool core_m14_compound_bool(_Bool value, _Bool right);
int core_m14_single_lvalue(int *value, int right);
_Bool core_m14_linux_tail(int value);

static int pick_calls;

int *core_m14_pick(int *value) {
    ++pick_calls;
    return value;
}

_Bool core_m14_report(int value) {
    return value != 0;
}

int main(void) {
    int value = 127;
    int single_result;

    single_result = core_m14_single_lvalue(&value, 15);
    printf("%d %d %u %d %d %d %d %d %d\n",
           core_m14_bitwise_and(0x5a, 0x3c),
           core_m14_compound_int(0x7f, 0x35),
           (unsigned int)core_m14_compound_uchar(0xf3U, 0x5aU),
           core_m14_compound_bool(1, 1),
           core_m14_compound_bool(1, 0),
           single_result,
           value,
           pick_calls,
           (int)core_m14_linux_tail(5) * 10 + (int)core_m14_linux_tail(0));
    return 0;
}
'''
)

Path("tests/compiler/c0/run-core-integer-bitwise-and-assignment.sh").write_text(
    r'''#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-integer-bitwise-and-assignment}"
source_file="$root/tests/compiler/c0/core_integer_bitwise_and_assignment.c"
runtime_file="$root/tests/compiler/c0/core_integer_bitwise_and_assignment_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
for symbol in core_m14_bitwise_and core_m14_compound_int core_m14_compound_uchar \
              core_m14_compound_bool core_m14_single_lvalue core_m14_linux_tail; do
    grep -q "^${symbol}:" "$work/core.s"
done
"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-integer-bitwise-and-assignment'
'''
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    """core_scalar_not_equal_focused() {\n""",
    """core_integer_bitwise_and_assignment_focused() {\n    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n    BUILD_DIR=\"$root/build/ci-core-integer-bitwise-and-assignment\" \\\n    RISCV_CC=riscv64-linux-gnu-gcc \\\n    QEMU_RISCV64=qemu-riscv64 \\\n        sh tests/compiler/c0/run-core-integer-bitwise-and-assignment.sh\n}\n\ncore_scalar_not_equal_focused() {\n""",
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    """start_gate core-scalar-not-equal-focused core_scalar_not_equal_focused\n""",
    """start_gate core-scalar-not-equal-focused core_scalar_not_equal_focused\nstart_gate core-integer-bitwise-and-assignment-focused core_integer_bitwise_and_assignment_focused\n""",
)

print("staged Core integer bitwise AND and compound assignment M14")
