#include "target/riscv64/runtime_libcall.h"
#include "target/riscv/instruction_select.h"
#include "frontend/type.h"

#include <stdio.h>

/* Strict RV64I chooses the existing compiler-runtime ABI, never an
 * implementation-specific algorithm in MiniC. The non-strict and RV64IM
 * branches retain their exact existing single-instruction output. */
bool minic_riscv64_uses_arithmetic_libcall(const MinicRiscvIsa *isa) {
    MinicRiscvIntegerPlan plan;
    return minic_riscv_plan_integer_arithmetic(
               isa, MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY,
               false, 64U, &plan) &&
           plan.strategy == MINIC_RISCV_INTEGER_RUNTIME_LIBCALL;
}
/* This is just the historical RV64 public operation classification. The
 * native M versus compiler-rt helper decision itself is shared with RV32. */
static bool rv64_arithmetic_kind(MinicRiscv64ArithmeticLibcall operation,
                                 MinicCoreInstructionKind *kind,
                                 bool *is_unsigned) {
    if (kind == NULL || is_unsigned == NULL) return false;
    *is_unsigned = false;
    switch (operation) {
    case MINIC_RISCV64_LIBCALL_MUL:
        *kind = MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY; break;
    case MINIC_RISCV64_LIBCALL_SDIV:
        *kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE; break;
    case MINIC_RISCV64_LIBCALL_UDIV:
        *kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE;
        *is_unsigned = true; break;
    case MINIC_RISCV64_LIBCALL_SMOD:
        *kind = MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER; break;
    case MINIC_RISCV64_LIBCALL_UMOD:
        *kind = MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER;
        *is_unsigned = true; break;
    default:
        return false;
    }
    return true;
}
/* GCC libgcc / compiler-rt standard SF/DF binary arithmetic helpers. The
 * RISC-V LP64 soft ABI passes the 32/64-bit payload in a0/a1, returning
 * its bits in a0. These are not included in our integer-only archive;
 * the final link must supply an RV64I+LP64 soft-float runtime build. */
const char *minic_riscv64_softfloat_binary_symbol(
    const MinicRiscvIsa *isa, MinicCoreInstructionKind kind) {
    if (isa == NULL || !isa->strict_isa ||
        isa->abi != MINIC_RISCV_ABI_LP64)
        return NULL;
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
        if (minic_riscv_codegen_has(isa, MINIC_RISCV_FEATURE_F)) return NULL;
        break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        if (minic_riscv_codegen_has(isa, MINIC_RISCV_FEATURE_D)) return NULL;
        break;
    default: return NULL;
    }
    return minic_riscv_softfloat_arithmetic_symbol(kind);
}

bool minic_riscv64_needs_arithmetic_libcall(const MinicRiscvIsa *isa,
                                             const MinicCoreFunction *function) {
    size_t i;
    if (isa == NULL || !isa->strict_isa || function == NULL)
        return false;
    /* Frame requirements deliberately overapproximate unreachable blocks.
     * This can waste one saved-ra slot in strict-I mode, but can never
     * clobber the function's entry return address. */
    for (i = 0U; i < function->instruction_count; ++i) {
        const MinicCoreInstruction *instruction = &function->instructions[i];
        if (minic_riscv64_softfloat_binary_symbol(isa, instruction->kind) != NULL)
            return true;
        /* TI division and remainder always require compiler-rt, even on
         * RV64IM where native DIV handles only XLEN operands. Their call
         * must reserve the saved return-address slot in every strict ABI. */
        if (minic_type_is_int128_integer(instruction->type) &&
            (instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE ||
             instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER))
            return true;
        /* Native M never helps a two-word comparison. Existing compiler-rt
         * helpers are the Base-I fallback on both rv64i and rv64im; the
         * frame must therefore reserve ra on either selected profile. */
        if ((instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS ||
             instruction->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL) &&
            instruction->value.binary.left < function->value_count &&
            minic_type_is_int128_integer(
                function->values[instruction->value.binary.left].type))
            return true;
        if (!minic_riscv64_uses_arithmetic_libcall(isa)) continue;
        switch (instruction->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
            /* __multi3 is the 128-bit RV64 LP64 runtime entry point. */
            return true;
        case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
            if (instruction->value.integer_overflow.operator_kind ==
                MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) return true;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
        case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
            return true;
        case MINIC_CORE_INSTRUCTION_POINTER_OFFSET: {
            MinicRiscvPointerScalePlan scale;
            if (!minic_riscv_plan_pointer_scale(
                    instruction->value.pointer_offset.element_size, &scale) ||
                scale.kind == MINIC_RISCV_POINTER_SCALE_MULTIPLY)
                return true;
            break;
        }
        default: break;
        }
    }
    return false;
}
bool minic_riscv64_emit_arithmetic_libcall(
    FILE *file, const MinicRiscvIsa *isa, MinicRiscv64ArithmeticLibcall operation,
    const char *destination, const char *left, const char *right) {
    MinicCoreInstructionKind kind;
    MinicRiscvIntegerPlan plan;
    bool is_unsigned;
    if (file == NULL || destination == NULL || left == NULL || right == NULL ||
        !rv64_arithmetic_kind(operation, &kind, &is_unsigned) ||
        !minic_riscv_plan_integer_arithmetic(
            isa, kind, is_unsigned, 64U, &plan))
        return false;
    if (plan.strategy == MINIC_RISCV_INTEGER_NATIVE_M)
        return fprintf(file, "  %s %s, %s, %s\n",
                       plan.opcode, destination, left, right) >= 0;
    if (plan.strategy != MINIC_RISCV_INTEGER_RUNTIME_LIBCALL)
        return false;
    /* LP64 register-call ABI remains owned by RV64, never by the selector.
     * This invariant also retains the original saved-ra frame requirement. */
    return fprintf(file,
                   "  mv a0, %s\n"
                   "  mv a1, %s\n"
                   "  call %s\n"
                   "  mv %s, a0\n",
                   left, right, plan.symbol, destination) >= 0;
}
