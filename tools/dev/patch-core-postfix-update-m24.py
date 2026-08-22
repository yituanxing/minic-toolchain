#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()

prototype_anchor = '''static MinicCoreLowerStatus lower_condition_branch(MinicCoreLowerContext *context,
                                                   MinicExpressionId expression_id,
                                                   MinicSourceSpan span,
                                                   MinicCoreBlockId when_true,
                                                   MinicCoreBlockId when_false);
'''
prototype = prototype_anchor + '''static MinicCoreLowerStatus
lower_postfix_scalar_update(MinicCoreLowerContext *context,
                            const MinicExpression *expression,
                            MinicCoreValueId *value_id);
'''
if text.count(prototype_anchor) != 1:
    raise SystemExit(f'M24 prototype anchor count={text.count(prototype_anchor)}')
text = text.replace(prototype_anchor, prototype, 1)

expr_anchor = '''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_NEGATE) {
'''
expr_insert = '''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT)) {
        return lower_postfix_scalar_update(context, expression, value_id);
    }
''' + expr_anchor
if text.count(expr_anchor) != 1:
    raise SystemExit(f'M24 expression anchor count={text.count(expr_anchor)}')
text = text.replace(expr_anchor, expr_insert, 1)

helper_begin = '''static MinicCoreLowerStatus
lower_discarded_postfix_integer_increment(MinicCoreLowerContext *context,
'''
helper_end = '''static MinicCoreLowerStatus lower_expression_statement(MinicCoreLowerContext *context,
'''
if text.count(helper_begin) != 1 or text.count(helper_end) != 1:
    raise SystemExit('M24 old helper boundary mismatch')
start = text.index(helper_begin)
end = text.index(helper_end, start)
new_helper = r'''static MinicCoreLowerStatus
lower_postfix_scalar_update(MinicCoreLowerContext *context,
                            const MinicExpression *expression,
                            MinicCoreValueId *value_id) {
    const MinicExpression *operand;
    MinicCoreInstruction instruction;
    MinicCoreValueId address;
    MinicCoreValueId current;
    MinicCoreValueId delta;
    MinicCoreValueId one;
    MinicCoreValueId updated;
    MinicCoreLowerStatus status;
    MinicType stored_type;
    bool increment;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || value_id == NULL ||
        expression->kind != MINIC_EXPRESSION_UNARY ||
        (expression->value.unary.operator_kind != MINIC_UNARY_POST_INCREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_POST_DECREMENT)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    increment = expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT;
    operand = minic_c0_program_expression(context->body->program, expression->value.unary.operand);
    if (operand == NULL || operand->value_category != MINIC_VALUE_LVALUE ||
        !core_memory_scalar_type(operand->type) || minic_type_is_const(operand->type) ||
        !minic_type_unqualified(operand->type, &stored_type) ||
        !minic_type_equal(expression->type, stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (minic_type_is_integer(stored_type) && minic_type_is_bool_integer(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_address(context, expression->value.unary.operand, &address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
    instruction.span = expression->span;
    instruction.type = stored_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.load.address = address;
    instruction.value.load.is_volatile = minic_type_is_volatile(operand->type);
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &current)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    if (minic_type_is_integer(stored_type)) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = 1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &one)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        delta = one;
        if (!increment) {
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_NEGATE;
            instruction.span = expression->span;
            instruction.type = stored_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.operand = one;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &delta)) {
                return MINIC_CORE_LOWER_ERROR;
            }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
        instruction.span = expression->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current;
        instruction.value.binary.right = delta;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &updated)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    } else if (minic_type_is_pointer(stored_type)) {
        size_t element_size;

        if (!minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      minic_default_data_layout(),
                                                      stored_type,
                                                      &element_size)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = increment ? 1 : -1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &delta)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        instruction.span = expression->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.pointer_offset.base = current;
        instruction.value.pointer_offset.index = delta;
        instruction.value.pointer_offset.element_size = element_size;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &updated)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    } else {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = expression->span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address;
    instruction.value.store.stored_value = updated;
    instruction.value.store.is_volatile = minic_type_is_volatile(operand->type);
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &instruction)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    *value_id = current;
    return MINIC_CORE_LOWER_OK;
}

'''
text = text[:start] + new_helper + text[end:]

old_stmt = '''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT) {
        return lower_discarded_postfix_integer_increment(context, expression);
    }
'''
new_stmt = '''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT)) {
        MinicCoreValueId discarded_value;

        return lower_postfix_scalar_update(context, expression, &discarded_value);
    }
'''
if text.count(old_stmt) != 1:
    raise SystemExit(f'M24 expression-statement anchor count={text.count(old_stmt)}')
text = text.replace(old_stmt, new_stmt, 1)
path.write_text(text)

Path('tests/compiler/c0/core_postfix_update_m24.c').write_text(r'''unsigned int core_m24_postdec_value(unsigned int *value) {
    return (*value)--;
}

unsigned int core_m24_countdown(unsigned int words) {
    unsigned int count = 0;
    while (words--) {
        count++;
    }
    return count;
}

unsigned short *core_m24_pointer_increment(unsigned short *pointer) {
    pointer++;
    return pointer;
}

unsigned short *core_m24_pointer_decrement(unsigned short *pointer) {
    pointer--;
    return pointer;
}

static void core_m24_touch(unsigned short *pointer) {
    *pointer = *pointer;
}

void core_m24_swab16_shape(unsigned short *buf, unsigned int words) {
    while (words--) {
        core_m24_touch(buf);
        buf++;
    }
}
''')
Path('tests/compiler/c0/core_postfix_update_m24_runtime.c').write_text(r'''extern unsigned int core_m24_postdec_value(unsigned int *);
extern unsigned int core_m24_countdown(unsigned int);
extern unsigned short *core_m24_pointer_increment(unsigned short *);
extern unsigned short *core_m24_pointer_decrement(unsigned short *);
extern void core_m24_swab16_shape(unsigned short *, unsigned int);

int main(void) {
    unsigned int value = 9;
    unsigned short data[4] = {1, 2, 3, 4};
    if (core_m24_postdec_value(&value) != 9 || value != 8) return 1;
    if (core_m24_countdown(7) != 7) return 2;
    if (core_m24_pointer_increment(&data[1]) != &data[2]) return 3;
    if (core_m24_pointer_decrement(&data[2]) != &data[1]) return 4;
    core_m24_swab16_shape(data, 4);
    if (data[0] != 1 || data[1] != 2 || data[2] != 3 || data[3] != 4) return 5;
    return 0;
}
''')
Path('tests/compiler/c0/run-core-postfix-update-m24.sh').write_text(r'''#!/bin/sh
set -eu
: "${MINIC:?set MINIC}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${BUILD_DIR:=build/core-postfix-update-m24}"
mkdir -p "$BUILD_DIR"
MINIC_CORE_IR=strict "$MINIC" -S tests/compiler/c0/core_postfix_update_m24.c -o "$BUILD_DIR/minic.s"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_postfix_update_m24_runtime.c "$BUILD_DIR/minic.s" -o "$BUILD_DIR/minic.elf"
"$QEMU_RISCV64" "$BUILD_DIR/minic.elf"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_postfix_update_m24_runtime.c tests/compiler/c0/core_postfix_update_m24.c -o "$BUILD_DIR/gcc.elf"
"$QEMU_RISCV64" "$BUILD_DIR/gcc.elf"
printf '%s\n' 'PASS compiler/c0/core-postfix-update-m24'
''')

gate_path = Path('.github/scripts/compiler-c0-full-gate.sh')
gate = gate_path.read_text()
function_anchor = 'runtime_record_fam_prefix_focused() {\n'
function_text = '''core_postfix_update_m24_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-postfix-update-m24" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-postfix-update-m24.sh
}

''' + function_anchor
if gate.count(function_anchor) != 1:
    raise SystemExit(f'M24 compiler gate function anchor count={gate.count(function_anchor)}')
gate = gate.replace(function_anchor, function_text, 1)
start_anchor = 'start_gate core-integer-bitwise-not-focused core_integer_bitwise_not_focused\n'
start_text = start_anchor + 'start_gate core-postfix-update-m24-focused core_postfix_update_m24_focused\n'
if gate.count(start_anchor) != 1:
    raise SystemExit(f'M24 compiler gate start anchor count={gate.count(start_anchor)}')
gate = gate.replace(start_anchor, start_text, 1)
gate_path.write_text(gate)

print('staged M24 scalar postfix update lowering')
