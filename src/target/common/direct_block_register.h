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

static inline bool minic_multi_direct_gp_adjacent_chain(
    const MinicCoreFunction *fn,
    const MinicCoreInstruction *producer,
    const MinicCoreInstruction *consumer,
    const size_t *value_use_counts) {
    MinicCoreValueId result;
    if (fn == NULL || producer == NULL || consumer == NULL ||
        value_use_counts == NULL ||
        !minic_multi_direct_gp_scalar_producer(producer->kind) ||
        !minic_multi_direct_gp_scalar_consumer(consumer, producer->result))
        return false;
    result = producer->result;
    return result != MINIC_CORE_VALUE_INVALID &&
           result < fn->value_count && value_use_counts[result] == 1U &&
           ((minic_type_is_integer(fn->values[result].type) &&
             !minic_type_is_int128_integer(fn->values[result].type)) ||
            minic_type_is_pointer(fn->values[result].type)) &&
           !minic_type_is_int128_integer(producer->type) &&
           !minic_type_is_int128_integer(consumer->type) &&
           (consumer->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION ||
            ((minic_type_is_integer(consumer->type) &&
              !minic_type_is_int128_integer(consumer->type)) ||
             minic_type_is_pointer(consumer->type)));
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
        !minic_multi_direct_gp_scalar_producer(producer->kind) ||
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
