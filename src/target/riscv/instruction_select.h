#ifndef MINIC_TARGET_RISCV_INSTRUCTION_SELECT_H
#define MINIC_TARGET_RISCV_INSTRUCTION_SELECT_H

#include "core/core_ir.h"
#include "target/riscv/isa_features.h"
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
/* Symbol identity for GCC libgcc soft integer arithmetic, keyed by
 * ISA register width. The consumer owns M-extension eligibility,
 * operand canonicalization and psABI register/stack transport. */
static inline const char *minic_riscv_integer_arithmetic_symbol(
    MinicCoreInstructionKind kind, bool is_unsigned, unsigned xlen) {
    if (xlen != 32U && xlen != 64U) return NULL;
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
        return xlen == 32U ? "__mulsi3" : "__muldi3";
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
        if (is_unsigned) return xlen == 32U ? "__udivsi3" : "__udivdi3";
        return xlen == 32U ? "__divsi3" : "__divdi3";
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
        if (is_unsigned) return xlen == 32U ? "__umodsi3" : "__umoddi3";
        return xlen == 32U ? "__modsi3" : "__moddi3";
    default: return NULL;
    }
}
/* Machine strategy for a single-XLEN integer Core operation.
 * The same decision works for RV32 and RV64. No global flags or implicit
 * policy changes: native M is only selected when the target explicitly
 * exposes it (or the historical non-strict RV64 default is active).
 * C/Z extensions do not change this operation's correctness path. */
typedef enum MinicRiscvIntegerStrategy {
    MINIC_RISCV_INTEGER_UNSUPPORTED = 0,
    MINIC_RISCV_INTEGER_NATIVE_M,
    MINIC_RISCV_INTEGER_RUNTIME_LIBCALL
} MinicRiscvIntegerStrategy;

typedef struct MinicRiscvIntegerPlan {
    MinicRiscvIntegerStrategy strategy;
    const char *opcode; /* only valid for NATIVE_M */
    const char *symbol; /* only valid for RUNTIME_LIBCALL */
} MinicRiscvIntegerPlan;

static inline bool minic_riscv_plan_integer_arithmetic(
    const MinicRiscvIsa *isa, MinicCoreInstructionKind kind,
    bool is_unsigned, unsigned xlen, MinicRiscvIntegerPlan *out) {
    bool has_native;
    const char *native;
    const char *runtime;
    if (out == NULL || (xlen != 32U && xlen != 64U) ||
        (isa != NULL && isa->xlen != xlen)) return false;
    out->strategy = MINIC_RISCV_INTEGER_UNSUPPORTED;
    out->opcode = NULL;
    out->symbol = NULL;
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY: native = "mul"; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
        native = is_unsigned ? "divu" : "div"; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
        native = is_unsigned ? "remu" : "rem"; break;
    default: return false;
    }
    runtime = minic_riscv_integer_arithmetic_symbol(kind, is_unsigned, xlen);
    if (runtime == NULL) return false;
    has_native = isa == NULL || minic_riscv_codegen_has(isa, MINIC_RISCV_FEATURE_M);
    out->strategy = has_native ? MINIC_RISCV_INTEGER_NATIVE_M :
                                  MINIC_RISCV_INTEGER_RUNTIME_LIBCALL;
    out->opcode = has_native ? native : NULL;
    out->symbol = has_native ? NULL : runtime;
    return true;
}
#endif
