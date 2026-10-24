#include "target/riscv64/runtime_libcall.h"
#include "target/riscv/instruction_select.h"
#include "frontend/type.h"

#include <stdio.h>

/* Strict RV64I chooses the existing compiler-runtime ABI, never an
 * implementation-specific algorithm in MiniC. The non-strict and RV64IM
 * branches retain their exact existing single-instruction output. */
bool minic_riscv64_uses_arithmetic_libcall(const MinicRiscvIsa *isa) {
    return isa != NULL && isa->strict_isa &&
           !minic_riscv_isa_has(isa, MINIC_RISCV_FEATURE_M);
}
bool minic_riscv64_power_of_two(size_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}
unsigned minic_riscv64_log2_power_of_two(size_t value) {
    unsigned shift = 0U;
    while (value > 1U) { value >>= 1U; ++shift; }
    return shift;
}
static const char *libcall_symbol(MinicRiscv64ArithmeticLibcall operation) {
    switch (operation) {
    case MINIC_RISCV64_LIBCALL_MUL: return "__muldi3";
    case MINIC_RISCV64_LIBCALL_SDIV: return "__divdi3";
    case MINIC_RISCV64_LIBCALL_UDIV: return "__udivdi3";
    case MINIC_RISCV64_LIBCALL_SMOD: return "__moddi3";
    case MINIC_RISCV64_LIBCALL_UMOD: return "__umoddi3";
    default: return NULL;
    }
}
static const char *native_opcode(MinicRiscv64ArithmeticLibcall operation) {
    switch (operation) {
    case MINIC_RISCV64_LIBCALL_MUL: return "mul";
    case MINIC_RISCV64_LIBCALL_SDIV: return "div";
    case MINIC_RISCV64_LIBCALL_UDIV: return "divu";
    case MINIC_RISCV64_LIBCALL_SMOD: return "rem";
    case MINIC_RISCV64_LIBCALL_UMOD: return "remu";
    default: return NULL;
    }
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
        if (minic_riscv_isa_has(isa, MINIC_RISCV_FEATURE_F)) return NULL;
        break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        if (minic_riscv_isa_has(isa, MINIC_RISCV_FEATURE_D)) return NULL;
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
        if (!minic_riscv64_uses_arithmetic_libcall(isa)) continue;
        switch (instruction->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
            /* __multi3 is the 128-bit RV64 LP64 runtime entry point. */
            return true;
        case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
            /* Soft-only two-limb integer comparisons use __cmpti2 or
             * __ucmpti2; plain scalar comparisons do not need a call. */
            if (instruction->value.binary.left < function->value_count &&
                minic_type_is_int128_integer(
                    function->values[instruction->value.binary.left].type))
                return true;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
            if (instruction->value.integer_overflow.operator_kind ==
                MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) return true;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
        case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
            return true;
        case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
            if (!minic_riscv64_power_of_two(
                    instruction->value.pointer_offset.element_size))
                return true;
            break;
        default: break;
        }
    }
    return false;
}
bool minic_riscv64_emit_arithmetic_libcall(
    FILE *file, const MinicRiscvIsa *isa, MinicRiscv64ArithmeticLibcall operation,
    const char *destination, const char *left, const char *right) {
    const char *symbol = libcall_symbol(operation);
    const char *native = native_opcode(operation);
    if (file == NULL || symbol == NULL || native == NULL || destination == NULL ||
        left == NULL || right == NULL)
        return false;
    if (!minic_riscv64_uses_arithmetic_libcall(isa))
        return fprintf(file, "  %s %s, %s, %s\n",
                       native, destination, left, right) >= 0;

    /* RV64 LP64 integer psABI: a0/a1 inputs, a0 result; caller-saved t*.
     * Prologue must already save ra, even for a function with no Core CALL.
     * The Core frame owns all live virtual values in stack slots. */
    return fprintf(file,
                   "  mv a0, %s\n"
                   "  mv a1, %s\n"
                   "  call %s\n"
                   "  mv %s, a0\n",
                   left, right, symbol, destination) >= 0;
}
