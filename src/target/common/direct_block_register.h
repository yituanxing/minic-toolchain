#ifndef MINIC_TARGET_COMMON_DIRECT_BLOCK_REGISTER_H
#define MINIC_TARGET_COMMON_DIRECT_BLOCK_REGISTER_H

#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>

/* First, deliberately small block-register location contract: two adjacent
 * pure scalar GP operations can pass a single-use SSA value directly through
 * the target's result register. This is a Core liveness/location decision,
 * not an assembler text substitution. Each backend must explicitly opt in
 * only if its registered operations leave the result in one known GP register
 * and consume the next left operand from that same register. No forwarding
 * crosses an instruction, block, call, asm, or control-flow boundary. */
static inline bool minic_multi_direct_gp_binary_consumer_opcode(
    MinicCoreInstructionKind kind) {
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        return true;
    default:
        return false;
    }
}

static inline bool minic_multi_direct_gp_unary_consumer_opcode(
    MinicCoreInstructionKind kind) {
    return kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE ||
           kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT ||
           kind == MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
}

/* LoongArch and x86-64 currently opt in to the same Core GP forwarding
 * proof: their eligible native scalar producers leave the result in $t0
 * and %rax respectively. AArch64 keeps its dispatch opt-out. The target
 * must still validate the actual register and width contracts. */
static inline bool minic_multi_direct_gp_scalar_producer(
    MinicCoreInstructionKind kind) {
    if (minic_multi_direct_gp_binary_consumer_opcode(kind) ||
        minic_multi_direct_gp_unary_consumer_opcode(kind)) return true;
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return true;
    default:
        return false;
    }
}

static inline bool minic_multi_direct_gp_scalar_consumer(
    const MinicCoreInstruction *consumer, MinicCoreValueId result) {
    if (consumer == NULL) return false;
    if (minic_multi_direct_gp_binary_consumer_opcode(consumer->kind))
        return consumer->value.binary.left == result;
    switch (consumer->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return consumer->value.operand == result;
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return consumer->value.operand == result;
    default:
        return false;
}
}

static inline const MinicCoreType *minic_multi_direct_gp_core_type(
    const MinicCoreFunction *fn, MinicCoreTypeId id) {
    return fn != NULL && fn->type_context != NULL
        ? minic_core_type_get(fn->type_context,
            minic_core_type_unqualified(fn->type_context, id)) : NULL;
}

static inline bool minic_multi_direct_gp_scalar_type(
    const MinicCoreFunction *fn, MinicCoreTypeId id) {
    const MinicCoreType *type = minic_multi_direct_gp_core_type(fn, id);
    return type != NULL && (type->kind == MINIC_CORE_TYPE_POINTER ||
        (type->kind == MINIC_CORE_TYPE_INTEGER && type->has_layout &&
         type->size != 0U && type->size <= 8U &&
         type->value.integer.bit_width <= 64U));
}

static inline bool minic_multi_direct_gp_not_int128(
    const MinicCoreFunction *fn, MinicCoreTypeId id) {
    const MinicCoreType *type = minic_multi_direct_gp_core_type(fn, id);
    return type != NULL && !(type->kind == MINIC_CORE_TYPE_INTEGER &&
                             type->value.integer.bit_width == 128U);
}

static inline bool minic_multi_direct_gp_adjacent_chain(
    const MinicCoreFunction *fn,
    const MinicCoreInstruction *producer,
    const MinicCoreInstruction *consumer,
    const size_t *value_use_counts) {
    MinicCoreValueId result;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        producer == NULL || consumer == NULL ||
        value_use_counts == NULL ||
        !minic_multi_direct_gp_scalar_producer(producer->kind) ||
        !minic_multi_direct_gp_scalar_consumer(consumer, producer->result))
        return false;
    result = producer->result;
    return result != MINIC_CORE_VALUE_INVALID &&
           result < fn->value_count && value_use_counts[result] == 1U &&
           minic_multi_direct_gp_scalar_type(fn,
               fn->values[result].core_type_id) &&
           minic_multi_direct_gp_not_int128(fn, producer->core_type_id) &&
           minic_multi_direct_gp_not_int128(fn, consumer->core_type_id) &&
           (consumer->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION ||
            minic_multi_direct_gp_scalar_type(fn, consumer->core_type_id));
}

/* A scalar LOAD or pure integer ALU result followed by its only STORE
 * consumer can pass the GP directly. Actual memory effects still execute,
 * even for volatile addresses; only the intermediate SSA copy is gone. */
static inline bool minic_multi_direct_gp_adjacent_result_store(
    const MinicCoreFunction *fn,
    const MinicCoreInstruction *producer,
    const MinicCoreInstruction *consumer,
    const size_t *value_use_counts) {
    MinicCoreValueId result;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        producer == NULL || consumer == NULL ||
        value_use_counts == NULL ||
        !minic_multi_direct_gp_scalar_producer(producer->kind) ||
        consumer->kind != MINIC_CORE_INSTRUCTION_STORE)
        return false;
    result = producer->result;
    if (result == MINIC_CORE_VALUE_INVALID || result >= fn->value_count ||
        value_use_counts[result] != 1U ||
        consumer->value.store.stored_value != result)
        return false;
    return minic_multi_direct_gp_scalar_type(fn,
               fn->values[result].core_type_id);
}

static inline bool minic_multi_direct_gp_adjacent_forward(
    const MinicCoreFunction *fn,
    const MinicCoreInstruction *producer,
    const MinicCoreInstruction *consumer,
    const size_t *value_use_counts) {
    return minic_multi_direct_gp_adjacent_chain(
               fn, producer, consumer, value_use_counts) ||
           minic_multi_direct_gp_adjacent_result_store(
               fn, producer, consumer, value_use_counts);
}

#endif
