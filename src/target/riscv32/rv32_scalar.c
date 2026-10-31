#include "target/riscv32/rv32_internal.h"
#include "target/riscv/typed_memory.h"
#include <stdint.h>

/* ILP32 machine scalar classification and width normalization.
 * A wide C scalar is still one Core SSA value in two XLEN words. */
bool rv32_scalar_storage_words(const MinicCoreFunction *f,
                                       MinicType ty, unsigned *words) {
    MinicType effective = ty;
    if (minic_type_is_double(ty)) { *words = 2U; return true; }
    if (minic_type_is_float(ty) || minic_type_is_pointer(ty)) {
        *words = 1U; return true;
    }
    if (minic_type_is_enum(ty) &&
        !minic_core_function_effective_integer_type(f, ty, &effective))
        return false;
    if (!minic_type_is_integer(effective)) return false;
    switch (effective.integer_rank) {
    case MINIC_INTEGER_RANK_LONG_LONG: *words = 2U; return true;
    case MINIC_INTEGER_RANK_BOOL:
    case MINIC_INTEGER_RANK_CHAR:
    case MINIC_INTEGER_RANK_SHORT:
    case MINIC_INTEGER_RANK_INT:
    case MINIC_INTEGER_RANK_LONG: *words = 1U; return true;
    default: return false;
    }
}
bool rv32_scalar(const MinicCoreFunction *f, MinicType t,
                         unsigned *bits, bool *sign) {
    MinicType effective = t;
    if (minic_type_is_pointer(t) || minic_type_is_float(t)) {
        *bits = 32U;
        *sign = false;
        return true;
    }
    if (!minic_type_is_integer(t) ||
        (minic_type_is_enum(t) &&
          !minic_core_function_effective_integer_type(f, t, &effective)))
        return false;
    switch (effective.integer_rank) {
    case MINIC_INTEGER_RANK_BOOL: *bits = 1U; break;
    case MINIC_INTEGER_RANK_CHAR: *bits = 8U; break;
    case MINIC_INTEGER_RANK_SHORT: *bits = 16U; break;
    case MINIC_INTEGER_RANK_INT:
    case MINIC_INTEGER_RANK_LONG: *bits = 32U; break;
    default: return false;
    }
    /* Plain char already carries the invocation's effective signedness.
     * Rejecting is_plain_char here silently breaks -fsigned-char loads. */
    *sign = effective.integer_sign == MINIC_INTEGER_SIGN_SIGNED;
    return true;
}
bool rv32_canon(FILE *out, const MinicCoreFunction *f,
                        MinicType t, const char *reg) {
    unsigned bits;
    bool is_signed;
    if (!rv32_scalar(f, t, &bits, &is_signed)) return false;
    if (bits == 32U) return true;
    if (bits == 1U)
        return fprintf(out, "  snez %s, %s\n", reg, reg) >= 0;
    return fprintf(out, "  slli %s, %s, %u\n  %s %s, %s, %u\n",
                   reg, reg, 32U - bits,
                   is_signed ? "srai" : "srli",
                   reg, reg, 32U - bits) >= 0;
}
bool rv32_load_mem(FILE *out, const MinicCoreFunction *f,
                   MinicType t, const char *dst, size_t alignment) {
    unsigned bits;
    bool is_signed;
    const char *op;
    if (!rv32_scalar(f, t, &bits, &is_signed)) return false;
    op = bits == 8U ? (is_signed ? "lb" : "lbu") :
         bits == 16U ? (is_signed ? "lh" : "lhu") :
         bits == 1U ? "lbu" : "lw";
    return minic_riscv_emit_typed_memory(
        out, false, 32U, bits <= 8U ? 1U : bits / 8U,
        is_signed, op, dst, "t0", "t3", alignment, "rv32_scalar");
}
bool rv32_store_mem(FILE *out, const MinicCoreFunction *f,
                    MinicType t, size_t alignment) {
    unsigned bits;
    bool is_signed;
    if (!rv32_scalar(f, t, &bits, &is_signed)) return false;
    (void)is_signed;
    return minic_riscv_emit_typed_memory(
        out, true, 32U, bits <= 8U ? 1U : bits / 8U,
        false, bits <= 8U ? "sb" : bits <= 16U ? "sh" : "sw",
        "t1", "t0", "t3", alignment, "rv32_scalar");
}
bool rv32_load2(FILE *out, const Rv32Frame *fr,
                        const MinicCoreInstruction *ins) {
    return rv32_slot(out, fr, ins->value.binary.left, "t0", true) &&
           rv32_slot(out, fr, ins->value.binary.right, "t1", true);
}
/* One target ABI algorithm serves both stack allocation and call emission.
 * Stack output is staged before touching a0-a7. */
