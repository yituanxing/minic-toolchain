#include "target/riscv32/rv32_internal.h"
#include "target/riscv/instruction_select.h"
#include <inttypes.h>
#include <stdint.h>

/* RV32 integer machine selection and ILP32 normalization. Common
 * RISC-V opcode and runtime symbol identities remain XLEN-parametric. */
bool rv32_emit_integer_instruction(FILE *out, const MinicCoreFunction *f,
                                   const Rv32Frame *fr,
                                   const MinicCoreInstruction *ins,
                                   const char *symbol) {
    const char *op = NULL;
    MinicType lhs;
    unsigned bits;
    bool is_signed;
    if (out == NULL || f == NULL || fr == NULL || ins == NULL) return false;
    /* Keep Core's wide arithmetic/compare/shift lowering as one machine
     * family; the scalar path must never truncate a 2*XLEN value. */
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        if (rv32_is_wide_integer(f, ins->type))
            return rv32_emit_wide_integer(out, f, fr, ins, symbol);
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (ins->value.binary.left < f->value_count &&
            rv32_is_wide_integer(f, f->values[ins->value.binary.left].type))
            return rv32_emit_wide_integer(out, f, fr, ins, symbol);
        break;
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        if (ins->value.operand < f->value_count &&
            rv32_is_wide_integer(f, f->values[ins->value.operand].type))
            return rv32_emit_wide_integer(out, f, fr, ins, symbol);
        break;
    default: break;
    }
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT: {
        unsigned words;
        if (!rv32_scalar_storage_words(f, ins->type, &words))
            return false;
        if (words == 2U) {
            uint64_t raw = (uint64_t)ins->value.integer_value;
            return fprintf(out, "  li t0, %" PRIu32 "\n  li t1, %" PRIu32 "\n",
                           (uint32_t)raw, (uint32_t)(raw >> 32U)) >= 0 &&
                   rv32_pair_slot(out, fr, ins->result, "t0", "t1", false);
        }
        return rv32_scalar(f, ins->type, &bits, &is_signed) &&
               fprintf(out, "  li t0, %" PRId64 "\n",
                       ins->value.integer_value) >= 0 &&
               rv32_canon(out, f, ins->type, "t0") &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    /* Core owns widths; both XLEN variants share base-I opcode names. */
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        op = minic_riscv_scalar_binary_opcode(ins->kind, false);
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        if (ins->value.binary.left >= f->value_count) return false;
        lhs = f->values[ins->value.binary.left].type;
        if (!rv32_scalar(f, lhs, &bits, &is_signed)) return false;
        op = minic_riscv_scalar_binary_opcode(
            ins->kind, ins->kind == MINIC_CORE_INSTRUCTION_POINTER_LESS ||
                           !is_signed);
        break;
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        return rv32_load2(out, fr, ins) &&
               fprintf(out, "  xor t0, t0, t1\n  seqz t0, t0\n") >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {
        /* Compiler target lowering only chooses an ABI-compatible libcall.
         * Actual arithmetic lives in the pinned GNU libgcc 14.2.0 sources,
         * not a second implementation in MiniC's instruction generator. */
        const char *helper;
        if (!rv32_scalar(f, ins->type, &bits, &is_signed))
            return false;
        helper = minic_riscv_integer_arithmetic_symbol(
            ins->kind, !is_signed, 32U);
        if (helper == NULL) return false;
        return rv32_load2(out, fr, ins) &&
               fprintf(out, "  mv a0, t0\n  mv a1, t1\n  call %s\n",
                       helper) >= 0 &&
               rv32_canon(out, f, ins->type, "a0") &&
               rv32_slot(out, fr, ins->result, "a0", false);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST: {
        const MinicCoreValueId id = ins->value.operand;
        unsigned from_words, to_words;
        if (id >= f->value_count ||
            !rv32_scalar_storage_words(f, f->values[id].type, &from_words) ||
            !rv32_scalar_storage_words(f, ins->type, &to_words))
            return false;
        if (from_words == 2U) {
            if (!rv32_pair_slot(out, fr, id, "t0", "t1", true)) return false;
        } else if (!rv32_slot(out, fr, id, "t0", true)) return false;
        if (to_words == 2U) {
            if (from_words == 1U) {
                if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_BITCAST ||
                    !rv32_scalar(f, f->values[id].type,
                                 &bits, &is_signed) ||
                    fprintf(out, is_signed ? "  srai t1, t0, 31\n" :
                                          "  li t1, 0\n") < 0)
                    return false;
            }
            return rv32_pair_slot(out, fr, ins->result,
                                  "t0", "t1", false);
        }
        return rv32_canon(out, f, ins->type, "t0") &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return rv32_slot(out, fr, ins->value.operand, "t0", true) &&
               fprintf(out, ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE ?
                       "  neg t0, t0\n" :
                       ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT ?
                       "  not t0, t0\n" : "  seqz t0, t0\n") >= 0 &&
               rv32_canon(out, f, ins->type, "t0") &&
               rv32_slot(out, fr, ins->result, "t0", false);
    default:
        return false;
    }
    return op != NULL && rv32_load2(out, fr, ins) &&
           fprintf(out, "  %s t0, t0, t1\n", op) >= 0 &&
           rv32_canon(out, f, ins->type, "t0") &&
           rv32_slot(out, fr, ins->result, "t0", false);
}
