#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()

helper_anchor = '''static MinicCoreLowerStatus reload_scalar_value(MinicCoreLowerContext *context,
'''
if text.count(helper_anchor) != 1:
    raise SystemExit(f'M23 helper anchor count={text.count(helper_anchor)}')
helper = r'''static MinicCoreLowerStatus store_scalar_value(MinicCoreLowerContext *context,
                                               MinicSourceSpan span,
                                               MinicType type,
                                               MinicCoreObjectId object_id,
                                               MinicCoreValueId value_id) {
    MinicCoreInstruction instruction;
    MinicCoreValueId address_id;
    MinicType pointer_type;

    if (context == NULL || context->function == NULL || !core_memory_scalar_type(type) ||
        minic_type_is_const(type) || minic_type_is_volatile(type) ||
        object_id >= context->function->object_count || value_id >= context->function->value_count ||
        !minic_type_equal(context->function->objects[object_id].type, type) ||
        !minic_type_equal(context->function->values[value_id].type, type) ||
        !minic_type_pointer_to(type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &address_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address_id;
    instruction.value.store.stored_value = value_id;
    instruction.value.store.is_volatile = false;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

'''
text = text.replace(helper_anchor, helper + helper_anchor, 1)

conditional_anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND) {
'''
if text.count(conditional_anchor) != 2:
    raise SystemExit(f'M23 conditional anchor count={text.count(conditional_anchor)}')
conditional = r'''    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {
        const MinicExpression *false_expression;
        const MinicExpression *true_expression;
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreObjectId result_object;
        MinicCoreValueId arm_value;
        MinicCoreLowerStatus status;
        MinicType false_type;
        MinicType true_type;

        if (expression->value.conditional.uses_condition_value ||
            expression->value.conditional.when_true == MINIC_EXPRESSION_INVALID ||
            expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
            !minic_type_is_integer(expression->type) || minic_type_is_const(expression->type) ||
            minic_type_is_volatile(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        true_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.when_true);
        false_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.when_false);
        if (true_expression == NULL || false_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!core_scalar_expression_value_type(context->body, true_expression, &true_type) ||
            !core_scalar_expression_value_type(context->body, false_expression, &false_type) ||
            !minic_type_equal(true_type, expression->type) ||
            !minic_type_equal(false_type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, expression->type, &result_object) ||
            !minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        status = lower_condition_branch(context,
                                        expression->value.conditional.condition,
                                        expression->span,
                                        true_block,
                                        false_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = true_block;
        status = lower_expression(context, expression->value.conditional.when_true, &arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = store_scalar_value(
            context, true_expression->span, expression->type, result_object, arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = false_block;
        status = lower_expression(context, expression->value.conditional.when_false, &arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = store_scalar_value(
            context, false_expression->span, expression->type, result_object, arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = merge_block;
        return reload_scalar_value(
            context, expression->span, expression->type, result_object, value_id);
    }
'''
conditional_position = text.rfind(conditional_anchor)
if conditional_position < 0:
    raise SystemExit('M23 value conditional anchor disappeared')
text = text[:conditional_position] + conditional + text[conditional_position:]
path.write_text(text)

source = Path('tests/compiler/c0/core_integer_conditional_m23.c')
source.write_text(r'''static unsigned long m23_swab64(unsigned long x) {
    return ((x & 0x00000000000000ffUL) << 56) |
           ((x & 0x000000000000ff00UL) << 40) |
           ((x & 0x0000000000ff0000UL) << 24) |
           ((x & 0x00000000ff000000UL) << 8) |
           ((x & 0x000000ff00000000UL) >> 8) |
           ((x & 0x0000ff0000000000UL) >> 24) |
           ((x & 0x00ff000000000000UL) >> 40) |
           ((x & 0xff00000000000000UL) >> 56);
}

unsigned long core_m23_choose(int condition, unsigned long when_true, unsigned long when_false) {
    return condition ? when_true : when_false;
}

unsigned long core_m23_swab_shape(unsigned long y) {
    return (unsigned long)(__builtin_constant_p(y) ?
        (((y & 0x00000000000000ffUL) << 56) |
         ((y & 0x000000000000ff00UL) << 40) |
         ((y & 0x0000000000ff0000UL) << 24) |
         ((y & 0x00000000ff000000UL) << 8) |
         ((y & 0x000000ff00000000UL) >> 8) |
         ((y & 0x0000ff0000000000UL) >> 24) |
         ((y & 0x00ff000000000000UL) >> 40) |
         ((y & 0xff00000000000000UL) >> 56)) :
        m23_swab64(y));
}
''')

runtime = Path('tests/compiler/c0/core_integer_conditional_m23_runtime.c')
runtime.write_text(r'''extern unsigned long core_m23_choose(int, unsigned long, unsigned long);
extern unsigned long core_m23_swab_shape(unsigned long);

int main(void) {
    if (core_m23_choose(0, 11UL, 22UL) != 22UL) return 1;
    if (core_m23_choose(1, 11UL, 22UL) != 11UL) return 2;
    if (core_m23_swab_shape(0x0123456789abcdefUL) != 0xefcdab8967452301UL) return 3;
    return 0;
}
''')

runner = Path('tests/compiler/c0/run-core-integer-conditional-m23.sh')
runner.write_text(r'''#!/bin/sh
set -eu
: "${MINIC:?set MINIC}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${BUILD_DIR:=build/core-integer-conditional-m23}"
mkdir -p "$BUILD_DIR"
MINIC_CORE_IR=strict "$MINIC" -S tests/compiler/c0/core_integer_conditional_m23.c -o "$BUILD_DIR/minic.s"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_integer_conditional_m23_runtime.c "$BUILD_DIR/minic.s" -o "$BUILD_DIR/minic.elf"
"$QEMU_RISCV64" "$BUILD_DIR/minic.elf"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_integer_conditional_m23_runtime.c tests/compiler/c0/core_integer_conditional_m23.c -o "$BUILD_DIR/gcc.elf"
"$QEMU_RISCV64" "$BUILD_DIR/gcc.elf"
printf '%s\n' 'PASS compiler/c0/core-integer-conditional-m23'
''')
print('staged M23 integer value conditional lowering')
