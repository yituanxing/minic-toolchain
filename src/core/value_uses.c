#include "core/value_uses.h"
#include <stdint.h>
#include <string.h>

/* Extracted without rewriting the tested RV64 Core SSA use enumeration. */
static bool core_visit_value_use(MinicCoreValueId value_id,
                                 MinicCoreValueUseVisitor visitor,
                                 void *context) {
    return visitor != NULL && value_id != MINIC_CORE_VALUE_INVALID &&
           visitor(value_id, context);
}

static bool core_visit_call_argument_uses(const MinicCoreFunction *function,
                                          size_t argument_begin,
                                          size_t argument_count,
                                          MinicCoreValueUseVisitor visitor,
                                          void *context) {
    size_t i;

    if (function == NULL || argument_begin > function->call_argument_count ||
        argument_count > function->call_argument_count - argument_begin) {
        return false;
    }
    for (i = 0U; i < argument_count; ++i) {
        const MinicCoreCallArgument *argument =
            &function->call_arguments[argument_begin + i];
        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE &&
            !core_visit_value_use(argument->value.value_id, visitor, context)) {
            return false;
        }
    }
    return true;
}

bool minic_core_visit_instruction_value_uses(const MinicCoreFunction *function,
                                              const MinicCoreInstruction *instruction,
                                              MinicCoreValueUseVisitor visitor,
                                              void *context) {
    size_t i;

    if (function == NULL || instruction == NULL || visitor == NULL) {
        return false;
    }
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        return core_visit_value_use(instruction->value.binary.left, visitor, context) &&
               core_visit_value_use(instruction->value.binary.right, visitor, context);

    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
        return core_visit_value_use(instruction->value.integer_overflow.left, visitor, context) &&
               core_visit_value_use(instruction->value.integer_overflow.right, visitor, context) &&
               core_visit_value_use(
                   instruction->value.integer_overflow.result_address, visitor, context);

    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR:
        return core_visit_value_use(instruction->value.operand, visitor, context);

    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE:
        return core_visit_value_use(
            instruction->value.fixed_register_write.source, visitor, context);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return core_visit_value_use(instruction->value.field_address.base, visitor, context);
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        return core_visit_value_use(instruction->value.pointer_offset.base, visitor, context) &&
               core_visit_value_use(instruction->value.pointer_offset.index, visitor, context);
    case MINIC_CORE_INSTRUCTION_LOAD:
        return core_visit_value_use(instruction->value.load.address, visitor, context);
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
        return core_visit_value_use(instruction->value.atomic_load.address, visitor, context);
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
        return core_visit_value_use(
                   instruction->value.atomic_store.address, visitor, context) &&
               core_visit_value_use(
                   instruction->value.atomic_store.stored_value, visitor, context);
    case MINIC_CORE_INSTRUCTION_STORE:
        return core_visit_value_use(instruction->value.store.address, visitor, context) &&
               core_visit_value_use(instruction->value.store.stored_value, visitor, context);
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
        return core_visit_value_use(
            instruction->value.record_load.source_address, visitor, context);
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY:
        return core_visit_value_use(
                   instruction->value.variadic_copy.destination, visitor, context) &&
               core_visit_value_use(
                   instruction->value.variadic_copy.source, visitor, context);
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        return core_visit_value_use(
                   instruction->value.record_copy.destination_address, visitor, context) &&
               core_visit_value_use(
                   instruction->value.record_copy.source_address, visitor, context);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
        return core_visit_value_use(
            instruction->value.register_output_input_inline_asm.operand, visitor, context);
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
        return core_visit_value_use(
                   instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address,
                   visitor,
                   context) &&
               core_visit_value_use(
                   instruction->value.memory_readwrite_scalar_input_inline_asm.operand,
                   visitor,
                   context);
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        return core_visit_value_use(
            instruction->value.scalar_input_inline_asm.operand, visitor, context);
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        if (instruction->value.structured_inline_asm.operand_count >
            MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
            return false;
        }
        for (i = 0U; i < instruction->value.structured_inline_asm.operand_count; ++i) {
            if (!core_visit_value_use(
                    instruction->value.structured_inline_asm.operands[i].value,
                    visitor,
                    context)) {
                return false;
            }
        }
        return true;
    case MINIC_CORE_INSTRUCTION_CALL:
        return core_visit_call_argument_uses(function,
                                             instruction->value.call.argument_begin,
                                             instruction->value.call.argument_count,
                                             visitor,
                                             context);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return core_visit_value_use(instruction->value.indirect_call.callee, visitor, context) &&
               core_visit_call_argument_uses(function,
                                             instruction->value.indirect_call.argument_begin,
                                             instruction->value.indirect_call.argument_count,
                                             visitor,
                                             context);

    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
    case MINIC_CORE_INSTRUCTION_PARAMETER:
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        return true;
    }
    return false;
}

bool minic_core_visit_terminator_value_uses(const MinicCoreTerminator *terminator,
                                             MinicCoreValueUseVisitor visitor,
                                             void *context) {
    if (terminator == NULL || visitor == NULL) {
        return false;
    }
    switch (terminator->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        return terminator->return_value == MINIC_CORE_VALUE_INVALID ||
               core_visit_value_use(terminator->return_value, visitor, context);
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        return core_visit_value_use(terminator->indirect_target, visitor, context);
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        return core_visit_value_use(terminator->conditional.condition, visitor, context);
    case MINIC_CORE_TERMINATOR_BRANCH:
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return true;
    }
    return false;
}

typedef struct CoreValueUseCountContext {
    size_t *counts;
    size_t capacity;
} CoreValueUseCountContext;

static bool core_value_count_use(MinicCoreValueId value, void *opaque) {
    CoreValueUseCountContext *context = (CoreValueUseCountContext *)opaque;
    if (context == NULL || context->counts == NULL ||
        value >= context->capacity || context->counts[value] == SIZE_MAX)
        return false;
    ++context->counts[value];
    return true;
}

bool minic_core_function_value_use_counts(const MinicCoreFunction *function,
    size_t *counts, size_t capacity) {
    CoreValueUseCountContext context;
    size_t block_id;
    size_t unused_slot = 0U;
    if (function == NULL || capacity < function->value_count ||
        function->value_count > SIZE_MAX / sizeof(*counts) ||
        (function->block_count != 0U && function->blocks == NULL) ||
        (function->value_count != 0U && counts == NULL))
        return false;
    context.counts = counts != NULL ? counts : &unused_slot;
    context.capacity = function->value_count;
    if (function->value_count != 0U)
        (void)memset(context.counts, 0,
                     function->value_count * sizeof(*counts));
    for (block_id = 0U; block_id < function->block_count; ++block_id) {
        const MinicCoreBlock *block = &function->blocks[block_id];
        size_t ordinal;
        if (!block->has_terminator ||
            (block->instruction_count != 0U && block->instructions == NULL))
            return false;
        for (ordinal = 0U; ordinal < block->instruction_count; ++ordinal) {
            const MinicCoreInstructionId instruction_id =
                block->instructions[ordinal];
            if (instruction_id >= function->instruction_count ||
                function->instructions == NULL ||
                !minic_core_visit_instruction_value_uses(
                    function, &function->instructions[instruction_id],
                    core_value_count_use, &context))
                return false;
        }
        if (!minic_core_visit_terminator_value_uses(
                &block->terminator, core_value_count_use, &context))
            return false;
    }
    return true;
}

