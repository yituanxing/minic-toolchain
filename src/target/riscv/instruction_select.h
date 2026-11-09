#ifndef MINIC_TARGET_RISCV_INSTRUCTION_SELECT_H
#define MINIC_TARGET_RISCV_INSTRUCTION_SELECT_H

#include "core/core_ir.h"
#include "target/common/core_numeric_shape.h"
#include "target/riscv/isa_features.h"
#include <stdbool.h>
#include <stddef.h>

/* RV64I checked multiply always lowers through __muldi3, and a 64-bit
 * result additionally calls __divdi3 / __udivdi3. Check the full supported
 * result shape before deciding whether the frame needs a saved RA. Keep
 * this predicate shared with emission: a malformed Core instruction may
 * fail closed, but may not accidentally turn a planned leaf into a caller. */
static inline bool minic_riscv_machine_checked_mul_runtime_call(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    unsigned xlen) {
    const MinicCoreTypeContext *types;
    const MinicCoreType *pointer, *flag, *result;
    MinicCoreTypeId pointee;
    MinicCoreValueId left, right, address;
    bool signed_left, signed_right, signed_result;
    if (fn == NULL || ins == NULL || xlen != 64U ||
        ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||
        ins->value.integer_overflow.operator_kind !=
            MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY ||
        fn->values == NULL || (types = fn->type_context) == NULL)
        return false;
    left = ins->value.integer_overflow.left;
    right = ins->value.integer_overflow.right;
    address = ins->value.integer_overflow.result_address;
    if (left >= fn->value_count || right >= fn->value_count ||
        address >= fn->value_count || ins->result >= fn->value_count)
        return false;
    pointer = minic_core_type_get(types, minic_core_type_unqualified(types,
        fn->values[address].core_type_id));
    flag = minic_core_type_get(types, minic_core_type_unqualified(types,
        ins->core_type_id));
    if (pointer == NULL || pointer->kind != MINIC_CORE_TYPE_POINTER ||
        !pointer->has_layout || pointer->size != 8U ||
        flag == NULL || flag->kind != MINIC_CORE_TYPE_INTEGER ||
        !flag->value.integer.is_bool ||
        minic_core_type_unqualified(types,
            fn->values[ins->result].core_type_id) !=
            minic_core_type_unqualified(types, ins->core_type_id))
        return false;
    pointee = minic_core_type_unqualified(types,
        pointer->value.pointer.pointee);
    result = minic_core_type_get(types, pointee);
    return result != NULL && result->kind == MINIC_CORE_TYPE_INTEGER &&
        result->has_layout && !result->value.integer.is_bool &&
        (result->size == 1U || result->size == 2U ||
         result->size == 4U || result->size == 8U) &&
        result->value.integer.bit_width == result->size * 8U &&
        ((minic_core_type_unqualified(types,
            fn->values[left].core_type_id) == pointee &&
          minic_core_type_unqualified(types,
            fn->values[right].core_type_id) == pointee) ||
         minic_core_integer_overflow_mixed_multiply_i32_shape(fn, ins,
             &signed_left, &signed_right, &signed_result));
}

/* The single Core→RISC-V typed Machine implicit-CALL inventory. The
 * physical selector and the pre-frame RA planner MUST consume the same
 * classification: MachineIR intentionally emits only base-I and therefore
 * uses compiler-runtime helpers even when -march exposes M/F/D. Explicit
 * Core CALLs are already accounted for by Common FunctionFacts. */
static inline bool minic_riscv_machine_implicit_runtime_call(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    unsigned xlen) {
    if (fn == NULL || ins == NULL || (xlen != 32U && xlen != 64U))
        return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
        return minic_riscv_machine_checked_mul_runtime_call(fn, ins, xlen);
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
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
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        return true;
    default:
        return false;
    }
}

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
/* R10: XLEN-neutral pointer stride classification. Instruction emission,
 * the ABI and return-address preservation remain target-owned. Core has
 * already evaluated index side effects for an empty GNU record stride. */
typedef enum MinicRiscvPointerScaleKind {
    MINIC_RISCV_POINTER_SCALE_ZERO = 0,
    MINIC_RISCV_POINTER_SCALE_UNIT,
    MINIC_RISCV_POINTER_SCALE_SHIFT,
    MINIC_RISCV_POINTER_SCALE_MULTIPLY
} MinicRiscvPointerScaleKind;

typedef struct MinicRiscvPointerScalePlan {
    MinicRiscvPointerScaleKind kind;
    unsigned shift; /* only meaningful for SHIFT, zero otherwise */
} MinicRiscvPointerScalePlan;

static inline bool minic_riscv_plan_pointer_scale(
    size_t stride, MinicRiscvPointerScalePlan *out) {
    unsigned shift = 0U;
    if (out == NULL) return false;
    out->shift = 0U;
    if (stride == 0U) {
        out->kind = MINIC_RISCV_POINTER_SCALE_ZERO;
    } else if (stride == 1U) {
        out->kind = MINIC_RISCV_POINTER_SCALE_UNIT;
    } else if ((stride & (stride - 1U)) != 0U) {
        out->kind = MINIC_RISCV_POINTER_SCALE_MULTIPLY;
    } else {
        while (stride > 1U) {
            stride >>= 1U;
            ++shift;
        }
        out->kind = MINIC_RISCV_POINTER_SCALE_SHIFT;
        out->shift = shift;
    }
    return true;
}

#endif
