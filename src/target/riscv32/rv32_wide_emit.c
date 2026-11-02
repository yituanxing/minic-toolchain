#include "target/riscv32/rv32_internal.h"
#include "target/riscv/instruction_select.h"
#include "target/riscv/word_operations.h"
#include <inttypes.h>
#include <stdint.h>

/* RV32I 2-XLEN integer machine lowering. One Core i64 is one semantic
 * scalar with low/high words in ILP32 frame and a0/a1 ABI.
 * No M/Z-extension instructions are synthesized here. */
static const MinicCoreType *rv32_core_wide_type(
    const MinicCoreFunction *f, MinicCoreTypeId id) {
    if (f == NULL || f->type_context == NULL) return NULL;
    return minic_core_type_get(f->type_context,
        minic_core_type_unqualified(f->type_context, id));
}

bool rv32_core_is_wide_integer(const MinicCoreFunction *f,
                               MinicCoreTypeId id) {
    const MinicCoreType *type;
    type = rv32_core_wide_type(f, id);
    return type != NULL && type->has_layout &&
           type->kind == MINIC_CORE_TYPE_INTEGER && type->size == 8U &&
           type->value.integer.bit_width == 64U &&
           !type->value.integer.is_bool;
}

bool rv32_core_unsigned_integer(const MinicCoreFunction *f,
                                MinicCoreTypeId id) {
    const MinicCoreType *type = rv32_core_wide_type(f, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           !type->value.integer.is_signed && !type->value.integer.is_bool;
}

bool rv32_core_integer_type(const MinicCoreFunction *f,
                            MinicCoreTypeId id) {
    const MinicCoreType *type = rv32_core_wide_type(f, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER;
}

bool rv32_core_pointer_type(const MinicCoreFunction *f,
                            MinicCoreTypeId id) {
    const MinicCoreType *type = rv32_core_wide_type(f, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_POINTER &&
           type->has_layout && type->size == 4U;
}

MinicCoreTypeId rv32_core_pointee_type(const MinicCoreFunction *f,
                                      MinicCoreTypeId id) {
    const MinicCoreType *type = rv32_core_wide_type(f, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_POINTER &&
        type->has_layout && type->size == 4U
        ? type->value.pointer.pointee : MINIC_CORE_TYPE_INVALID;
}

static bool wide_pair_operands(FILE *out, const MinicCoreFunction *f,
                               const Rv32Frame *fr,
                               const MinicCoreInstruction *ins) {
    MinicCoreValueId left = ins->value.binary.left;
    MinicCoreValueId right = ins->value.binary.right;
    return left < f->value_count && right < f->value_count &&
           rv32_core_is_wide_integer(f, f->values[left].core_type_id) &&
           rv32_core_is_wide_integer(f, f->values[right].core_type_id) &&
           rv32_pair_slot(out, fr, left, "t0", "t1", true) &&
           rv32_pair_slot(out, fr, right, "t2", "t3", true);
}

/* C permits any integer type for the shift count. The shared
 * pair-shift emitter handles ILP32 one-word and two-word counts (t2/t5),
 * including a nonzero upper half. */
static bool wide_shift(FILE *out, const MinicCoreFunction *f,
                       const Rv32Frame *fr, const MinicCoreInstruction *ins,
                       const char *symbol) {
    const MinicCoreValueId left = ins->value.binary.left;
    const MinicCoreValueId right = ins->value.binary.right;
    bool right_shift = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
    bool signed_shift;
    unsigned shift_words;
    if (symbol == NULL || left >= f->value_count ||
        right >= f->value_count ||
        !rv32_core_is_wide_integer(f, f->values[left].core_type_id) ||
        !rv32_core_integer_type(f, f->values[right].core_type_id) ||
        !rv32_core_scalar_storage_words(f,
             f->values[right].core_type_id, &shift_words))
        return false;
    signed_shift = right_shift &&
                   !rv32_core_unsigned_integer(f, f->values[left].core_type_id);
    if (!rv32_pair_slot(out, fr, left, "t0", "t1", true))
        return false;
    if (shift_words == 2U) {
        if (!rv32_pair_slot(out, fr, right, "t2", "t5", true))
            return false;
    } else if (!rv32_slot(out, fr, right, "t2", true)) {
        return false;
    }
    if (!minic_riscv_emit_word_pair_shift(
            out, ins->kind, signed_shift, symbol, "wide",
            ins->result, 32U, shift_words == 2U))
        return false;
    return rv32_pair_slot(out, fr, ins->result, "t0", "t1", false);
}

bool rv32_emit_wide_integer(FILE *out, const MinicCoreFunction *f,
                            const Rv32Frame *fr,
                            const MinicCoreInstruction *ins,
                            const char *symbol) {
    const MinicCoreInstructionKind kind = ins->kind;
    if (out == NULL || f == NULL || fr == NULL || ins == NULL)
        return false;
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        if (!rv32_core_is_wide_integer(f, ins->core_type_id) ||
            !wide_pair_operands(out, f, fr, ins)) return false;
        if (!minic_riscv_emit_word_pair_binary(out, kind)) return false;
        return rv32_pair_slot(out, fr, ins->result, "t0", "t1", false);
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT: {
        const MinicCoreValueId id = ins->value.operand;
        if (!rv32_core_is_wide_integer(f, ins->core_type_id) ||
            id >= f->value_count ||
            !rv32_core_is_wide_integer(f, f->values[id].core_type_id) ||
            !rv32_pair_slot(out, fr, id, "t0", "t1", true))
            return false;
        if (!minic_riscv_emit_word_pair_unary(out, kind)) return false;
        return rv32_pair_slot(out, fr, ins->result, "t0", "t1", false);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {
        /* Pinned original LLVM compiler-rt DI arithmetic. We select the
         * standard symbol and implement only the psABI register transfer. */
        const char *helper = minic_riscv_integer_arithmetic_symbol(
            kind, rv32_core_unsigned_integer(f, ins->core_type_id), 64U);
        if (helper == NULL ||
            !rv32_core_is_wide_integer(f, ins->core_type_id) ||
            !wide_pair_operands(out, f, fr, ins))
            return false;
        return fprintf(out,
                 "  mv a0, t0\n  mv a1, t1\n"
                 "  mv a2, t2\n  mv a3, t3\n"
                 "  call %s\n", helper) >= 0 &&
               rv32_pair_slot(out, fr, ins->result, "a0", "a1", false);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
        return rv32_core_is_wide_integer(f, ins->core_type_id) &&
               wide_shift(out, f, fr, ins, symbol);
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS: {
        bool is_unsigned;
        if (!wide_pair_operands(out, f, fr, ins)) return false;
        is_unsigned = rv32_core_unsigned_integer(f,
            f->values[ins->value.binary.left].core_type_id);
        if (fprintf(out,
                "  xor t4, t1, t3\n"
                "  seqz t4, t4\n"
                "  sltu t5, t0, t2\n"
                "  and t5, t5, t4\n"
                "  %s t0, t1, t3\n"
                "  or t0, t0, t5\n",
                is_unsigned ? "sltu" : "slt") < 0)
            return false;
        return rv32_slot(out, fr, ins->result, "t0", false);
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        return wide_pair_operands(out, f, fr, ins) &&
               fprintf(out,
                 "  xor t0, t0, t2\n"
                 "  xor t1, t1, t3\n"
                 "  or t0, t0, t1\n"
                 "  seqz t0, t0\n") >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO: {
        const MinicCoreValueId id = ins->value.operand;
        return id < f->value_count &&
               rv32_core_is_wide_integer(f, f->values[id].core_type_id) &&
               rv32_pair_slot(out, fr, id, "t0", "t1", true) &&
               fprintf(out, "  or t0, t0, t1\n  seqz t0, t0\n") >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    default: return false;
    }
}
