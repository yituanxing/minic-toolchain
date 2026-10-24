#ifndef MINIC_TARGET_RISCV_INSTRUCTION_SELECT_H
#define MINIC_TARGET_RISCV_INSTRUCTION_SELECT_H

#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>

/* Machine operation selection shared by RV32I and RV64.
 * Core/type legalization and XLEN-specific canonicalization belong to the
 * consumer. This layer selects only base-I register opcode identities;
 * it never places registers, frame slots or ABI arguments. */
static inline const char *minic_riscv_scalar_binary_opcode(
    MinicCoreInstructionKind kind, bool is_unsigned) {
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD: return "add";
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT: return "sub";
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND: return "and";
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR: return "or";
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR: return "xor";
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT: return "sll";
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
        return is_unsigned ? "srl" : "sra";
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        return is_unsigned ? "sltu" : "slt";
    case MINIC_CORE_INSTRUCTION_POINTER_LESS: return "sltu";
    default: return NULL;
    }
}

/* A standard symbol lookup, not a software arithmetic implementation.
 * Callers independently qualify strict ISA profiles and their own
 * ILP32/LP64 argument and result register transfer. */
static inline const char *minic_riscv_softfloat_arithmetic_symbol(
    MinicCoreInstructionKind kind) {
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD: return "__addsf3";
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT: return "__subsf3";
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY: return "__mulsf3";
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE: return "__divsf3";
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD: return "__adddf3";
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT: return "__subdf3";
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY: return "__muldf3";
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE: return "__divdf3";
    default: return NULL;
    }
}
#endif
