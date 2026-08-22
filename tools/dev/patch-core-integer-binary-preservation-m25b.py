#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def replace_between(text: str, start: str, end: str, replacement: str, label: str) -> str:
    start_pos = text.find(start)
    if start_pos < 0 or text.find(start, start_pos + 1) >= 0:
        raise SystemExit(f"{label}: start anchor not unique")
    end_pos = text.find(end, start_pos + len(start))
    if end_pos < 0:
        raise SystemExit(f"{label}: end anchor not found")
    return text[:start_pos] + replacement + text[end_pos:]


root = Path(__file__).resolve().parents[2]
core = root / "src/core/core_lower.c"
gate = root / ".github/scripts/compiler-c0-full-gate.sh"

text = core.read_text()
helper_anchor = """    *right_value = right_normalized;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_integer_assignment_value(MinicCoreLowerContext *context,
"""
helper = """    *right_value = right_normalized;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_integer_binary_operands(MinicCoreLowerContext *context,
                                                          MinicExpressionId left_id,
                                                          MinicExpressionId right_id,
                                                          MinicType result_type,
                                                          MinicCoreValueId *left_value,
                                                          MinicCoreValueId *right_value) {
    const MinicExpression *left_expression;
    const MinicExpression *right_expression;
    MinicCoreObjectId left_object;
    MinicCoreValueId left_normalized;
    MinicCoreValueId left_source;
    MinicCoreValueId right_normalized;
    MinicCoreValueId right_source;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || left_value == NULL || right_value == NULL ||
        !minic_type_is_integer(result_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    left_expression = minic_c0_program_expression(context->body->program, left_id);
    right_expression = minic_c0_program_expression(context->body->program, right_id);
    if (left_expression == NULL || right_expression == NULL ||
        !minic_type_is_integer(left_expression->type) ||
        !minic_type_is_integer(right_expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = lower_expression(context, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = append_integer_conversion(
        context, left_expression->span, result_type, left_source, &left_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = spill_scalar_value(
        context, left_expression->span, result_type, left_normalized, &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    status = lower_expression(context, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = append_integer_conversion(
        context, right_expression->span, result_type, right_source, &right_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = reload_scalar_value(
        context, left_expression->span, result_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    *right_value = right_normalized;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_integer_assignment_value(MinicCoreLowerContext *context,
"""
text = replace_once(text, helper_anchor, helper, "integer binary helper")

add_start = """    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
"""
and_start = """    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {
"""
or_start = """    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR) {
"""
shift_start = """    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT)) {
"""

add_replacement = add_start + """        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
"""
text = replace_between(text, add_start, and_start, add_replacement, "integer add")

and_replacement = and_start + """        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
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
"""
text = replace_between(text, and_start, or_start, and_replacement, "integer bitwise and")

or_replacement = or_start + """        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
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
"""
text = replace_between(text, or_start, shift_start, or_replacement, "integer bitwise or")
core.write_text(text)

focused_c = root / "tests/compiler/c0/core_integer_binary_preservation_m25b.c"
focused_c.write_text("""static unsigned short core_m25b_slow(unsigned short value) {
    return (unsigned short)(value + 1U);
}

unsigned short core_m25b_add(unsigned short *left, unsigned short *right) {
    return (unsigned short)((__builtin_constant_p(*left) ? *left : core_m25b_slow(*left)) +
                            (__builtin_constant_p(*right) ? *right : core_m25b_slow(*right)));
}

unsigned short core_m25b_and(unsigned short *left, unsigned short *right) {
    return (unsigned short)((__builtin_constant_p(*left) ? *left : core_m25b_slow(*left)) &
                            (__builtin_constant_p(*right) ? *right : core_m25b_slow(*right)));
}

unsigned short core_m25b_or(unsigned short *left, unsigned short *right) {
    return (unsigned short)((__builtin_constant_p(*left) ? *left : core_m25b_slow(*left)) |
                            (__builtin_constant_p(*right) ? *right : core_m25b_slow(*right)));
}

void core_m25b_be16_shape(unsigned short *value, unsigned short addend) {
    *value = __builtin_constant_p((unsigned short)((__builtin_constant_p(*value)
                 ? (unsigned short)(((*value & 0x00ffU) << 8) | ((*value & 0xff00U) >> 8))
                 : core_m25b_slow(*value)) + addend))
                 ? (unsigned short)(((((unsigned short)((__builtin_constant_p(*value)
                       ? (unsigned short)(((*value & 0x00ffU) << 8) | ((*value & 0xff00U) >> 8))
                       : core_m25b_slow(*value)) + addend)) & 0x00ffU) << 8) |
                                    ((((unsigned short)((__builtin_constant_p(*value)
                       ? (unsigned short)(((*value & 0x00ffU) << 8) | ((*value & 0xff00U) >> 8))
                       : core_m25b_slow(*value)) + addend)) & 0xff00U) >> 8))
                 : core_m25b_slow((unsigned short)((__builtin_constant_p(*value)
                       ? (unsigned short)(((*value & 0x00ffU) << 8) | ((*value & 0xff00U) >> 8))
                       : core_m25b_slow(*value)) + addend));
}
""")

runtime_c = root / "tests/compiler/c0/core_integer_binary_preservation_m25b_runtime.c"
runtime_c.write_text("""unsigned short core_m25b_add(unsigned short *left, unsigned short *right);
unsigned short core_m25b_and(unsigned short *left, unsigned short *right);
unsigned short core_m25b_or(unsigned short *left, unsigned short *right);
void core_m25b_be16_shape(unsigned short *value, unsigned short addend);

int main(void) {
    unsigned short left = 0x0012U;
    unsigned short right = 0x0034U;
    unsigned short value = 0x1234U;

    if (core_m25b_add(&left, &right) != 0x0048U) {
        return 1;
    }
    if (core_m25b_and(&left, &right) != 0x0011U) {
        return 2;
    }
    if (core_m25b_or(&left, &right) != 0x0037U) {
        return 3;
    }
    core_m25b_be16_shape(&value, 1U);
    if (value != 0x1236U) {
        return 4;
    }
    return 0;
}
""")

run_sh = root / "tests/compiler/c0/run-core-integer-binary-preservation-m25b.sh"
run_sh.write_text("""#!/bin/sh
set -eu
: "${MINIC:?set MINIC}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${BUILD_DIR:=build/core-integer-binary-preservation-m25b}"
mkdir -p "$BUILD_DIR"
MINIC_CORE_IR=strict "$MINIC" -S tests/compiler/c0/core_integer_binary_preservation_m25b.c -o "$BUILD_DIR/minic.s"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_integer_binary_preservation_m25b_runtime.c "$BUILD_DIR/minic.s" -o "$BUILD_DIR/minic.elf"
"$QEMU_RISCV64" "$BUILD_DIR/minic.elf"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_integer_binary_preservation_m25b_runtime.c tests/compiler/c0/core_integer_binary_preservation_m25b.c -o "$BUILD_DIR/gcc.elf"
"$QEMU_RISCV64" "$BUILD_DIR/gcc.elf"
printf '%s\n' 'PASS compiler/c0/core-integer-binary-preservation-m25b'
""")

gate_text = gate.read_text()
function_marker = "runtime_record_fam_prefix_focused() {\n"
focused_function = """core_integer_binary_preservation_m25b_focused() {
    MINIC="$root/build/ci-debug/bin/minic" BUILD_DIR="$root/build/ci-core-integer-binary-preservation-m25b" RISCV_CC=riscv64-linux-gnu-gcc QEMU_RISCV64=qemu-riscv64 sh tests/compiler/c0/run-core-integer-binary-preservation-m25b.sh
}

"""
gate_text = replace_once(
    gate_text,
    function_marker,
    focused_function + function_marker,
    "C0 M25b focused function marker",
)
start_anchor = "start_gate core-discard-expression-m25-focused core_discard_expression_m25_focused\n"
start_addition = start_anchor + "start_gate core-integer-binary-preservation-m25b-focused core_integer_binary_preservation_m25b_focused\n"
gate_text = replace_once(gate_text, start_anchor, start_addition, "C0 M25b start_gate")
gate.write_text(gate_text)
