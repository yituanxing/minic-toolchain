#include "target/riscv32/rv32_internal.h"
#include "target/riscv/instruction_select.h"
#include <inttypes.h>
#include <stdint.h>

/* RV32 soft-float arithmetic uses upstream LLVM compiler-rt IEEE-754,
 * with two-XLEN ILP32 register transport. No duplicated FP algorithms. */
bool rv32_emit_fp_instruction(FILE *out, const MinicCoreFunction *f,
                              const Rv32Frame *fr,
                              const MinicCoreInstruction *ins) {
    unsigned bits;
    bool is_signed;
    if (out == NULL || f == NULL || fr == NULL || ins == NULL) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT: {
        uint64_t raw = ins->value.floating_bits;
        if (minic_type_is_double(ins->type)) {
            return fprintf(out, "  li t0, %" PRIu32 "\n  li t1, %" PRIu32 "\n",
                           (uint32_t)raw, (uint32_t)(raw >> 32U)) >= 0 &&
                   rv32_pair_slot(out, fr, ins->result, "t0", "t1", false);
        }
        return minic_type_is_float(ins->type) &&
               fprintf(out, "  li t0, %" PRIu32 "\n", (uint32_t)raw) >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    /* Single-precision soft-float uses one ILP32 integer argument register
     * per operand (a0/a1), and returns the raw IEEE-754 bits in a0.
     * MiniC neither emits F instructions nor reimplements arithmetic. */
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE: {
        const char *helper = minic_riscv_softfloat_arithmetic_symbol(ins->kind);
        MinicCoreValueId left = ins->value.binary.left;
        MinicCoreValueId right = ins->value.binary.right;
        if (helper == NULL || !minic_type_is_float(ins->type) ||
            left >= f->value_count || right >= f->value_count ||
            !minic_type_is_float(f->values[left].type) ||
            !minic_type_is_float(f->values[right].type) ||
            !rv32_slot(out, fr, left, "a0", true) ||
            !rv32_slot(out, fr, right, "a1", true) ||
            fprintf(out, "  call %s\n", helper) < 0)
            return false;
        return rv32_slot(out, fr, ins->result, "a0", false);
    }
    /* All IEEE-754 arithmetic and comparisons are supplied by pinned LLVM
     * compiler-rt 19.1.7, not reimplemented as target machine instructions.
     * ILP32 soft-float ABI transfers each binary64 through a0/a1 and a2/a3. */
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL: {
        const char *helper = minic_riscv_softfloat_arithmetic_symbol(ins->kind);
        /* Comparisons use their own psABI predicate convention. */
        if (helper == NULL)
            helper = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL ?
                     "__eqdf2" : "__ledf2";
        if (ins->value.binary.left >= f->value_count ||
            ins->value.binary.right >= f->value_count ||
            !rv32_pair_slot(out, fr, ins->value.binary.left,
                            "a0", "a1", true) ||
            !rv32_pair_slot(out, fr, ins->value.binary.right,
                            "a2", "a3", true) ||
            fprintf(out, "  call %s\n", helper) < 0)
            return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL ||
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS ||
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL) {
            /* __ledf2: -1 (<), 0 (==), 1 (>) or 1 (NaN).
             * The predicates below therefore reject unordered comparisons. */
            const char *test =
                ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL ?
                "  seqz a0, a0\n" :
                ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS ?
                "  slt a0, a0, zero\n" : "  slti a0, a0, 1\n";
            return fprintf(out, "%s", test) >= 0 &&
                   rv32_slot(out, fr, ins->result, "a0", false);
        }
        return rv32_pair_slot(out, fr, ins->result, "a0", "a1", false);
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        return rv32_pair_slot(out, fr, ins->value.operand,
                              "t0", "t1", true) &&
               fprintf(out, "  li t2, 0x80000000\n  xor t1, t1, t2\n") >= 0 &&
               rv32_pair_slot(out, fr, ins->result, "t0", "t1", false);
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
        MinicCoreValueId id = ins->value.operand;
        MinicType effective;
        unsigned words;
        const char *helper;
        if (id >= f->value_count ||
            !rv32_scalar_storage_words(f, f->values[id].type, &words))
            return false;
        effective = f->values[id].type;
        if (minic_type_is_enum(effective) &&
            !minic_core_function_effective_integer_type(f, effective, &effective))
            return false;
        if (!minic_type_is_integer(effective)) return false;
        if (words == 2U) {
            is_signed = effective.integer_sign == MINIC_INTEGER_SIGN_SIGNED;
            helper = is_signed ? "__floatdidf" : "__floatundidf";
            if (!rv32_pair_slot(out, fr, id, "a0", "a1", true))
                return false;
        } else {
            if (!rv32_scalar(f, f->values[id].type, &bits, &is_signed) ||
                !rv32_slot(out, fr, id, "a0", true))
                return false;
            helper = is_signed ? "__floatsidf" : "__floatunsidf";
        }
        return fprintf(out, "  call %s\n", helper) >= 0 &&
               rv32_pair_slot(out, fr, ins->result, "a0", "a1", false);
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
        MinicType effective = ins->type;
        unsigned words;
        const char *helper;
        if (!rv32_scalar_storage_words(f, ins->type, &words) ||
            (minic_type_is_enum(effective) &&
             !minic_core_function_effective_integer_type(f, effective, &effective)) ||
            !minic_type_is_integer(effective))
            return false;
        if (words == 2U) {
            is_signed = effective.integer_sign == MINIC_INTEGER_SIGN_SIGNED;
            helper = is_signed ? "__fixdfdi" : "__fixunsdfdi";
        } else {
            if (!rv32_scalar(f, ins->type, &bits, &is_signed))
                return false;
            helper = is_signed ? "__fixdfsi" : "__fixunsdfsi";
        }
        if (!rv32_pair_slot(out, fr, ins->value.operand, "a0", "a1", true) ||
            fprintf(out, "  call %s\n", helper) < 0)
            return false;
        return words == 2U ?
            rv32_pair_slot(out, fr, ins->result, "a0", "a1", false) :
            (rv32_canon(out, f, ins->type, "a0") &&
             rv32_slot(out, fr, ins->result, "a0", false));
    }
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
        return rv32_slot(out, fr, ins->value.operand, "a0", true) &&
               fprintf(out, "  call __extendsfdf2\n") >= 0 &&
               rv32_pair_slot(out, fr, ins->result, "a0", "a1", false);
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
        return rv32_pair_slot(out, fr, ins->value.operand,
                              "a0", "a1", true) &&
               fprintf(out, "  call __truncdfsf2\n") >= 0 &&
               rv32_slot(out, fr, ins->result, "a0", false);
    default:
        return false;
    }
}
