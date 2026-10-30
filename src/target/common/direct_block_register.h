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
static inline bool minic_multi_direct_gp_chain_opcode(
    MinicCoreInstructionKind kind) {
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        return true;
    default:
        return false;
    }
}

static inline bool minic_multi_direct_gp_adjacent_chain(
    const MinicCoreFunction *fn,
    const MinicCoreInstruction *producer,
    const MinicCoreInstruction *consumer,
    const size_t *value_use_counts) {
    MinicCoreValueId result;
    if (fn == NULL || producer == NULL || consumer == NULL ||
        value_use_counts == NULL ||
        !minic_multi_direct_gp_chain_opcode(producer->kind) ||
        !minic_multi_direct_gp_chain_opcode(consumer->kind))
        return false;
    result = producer->result;
    return result != MINIC_CORE_VALUE_INVALID &&
           result < fn->value_count && value_use_counts[result] == 1U &&
           !minic_type_is_int128_integer(producer->type) &&
           !minic_type_is_int128_integer(consumer->type) &&
           consumer->value.binary.left == result;
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
    MinicType type;
    if (fn == NULL || producer == NULL || consumer == NULL ||
        value_use_counts == NULL ||
        (producer->kind != MINIC_CORE_INSTRUCTION_LOAD &&
         !minic_multi_direct_gp_chain_opcode(producer->kind)) ||
        consumer->kind != MINIC_CORE_INSTRUCTION_STORE)
        return false;
    result = producer->result;
    if (result == MINIC_CORE_VALUE_INVALID || result >= fn->value_count ||
        value_use_counts[result] != 1U ||
        consumer->value.store.stored_value != result)
        return false;
    type = fn->values[result].type;
    return (minic_type_is_integer(type) &&
            !minic_type_is_int128_integer(type)) ||
           minic_type_is_pointer(type);
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
