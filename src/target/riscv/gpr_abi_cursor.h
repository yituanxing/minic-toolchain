#ifndef MINIC_TARGET_RISCV_GPR_ABI_CURSOR_H
#define MINIC_TARGET_RISCV_GPR_ABI_CURSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RV32 and RV64 psABI share eight integer-argument lanes. Their byte
 * widths and eligibility/classification are independent of this cursor.
 * Caller supplies 1-2 XLEN words and requests an atomic even-register
 * pair only for an unnamed 2-XLEN value whose psABI requires it.
 * Named wide values may cross a7/stack0. Counts are in XLEN words. */
typedef struct MinicRiscvGprAbiCursor {
    size_t register_words;
    size_t stack_words;
    bool variadic_stack_only;
} MinicRiscvGprAbiCursor;

typedef struct MinicRiscvGprAbiPlacement {
    size_t register_begin;
    size_t register_count;
    size_t stack_begin;
    size_t stack_count;
} MinicRiscvGprAbiPlacement;

static inline bool minic_riscv_gpr_abi_place(
    MinicRiscvGprAbiCursor *cursor, size_t words,
    bool unnamed, bool even_register_pair,
    MinicRiscvGprAbiPlacement *location) {
    MinicRiscvGprAbiCursor next;
    MinicRiscvGprAbiPlacement loc;
    size_t available;
    if (cursor == NULL || location == NULL || words == 0U || words > 2U ||
        cursor->register_words > 8U ||
        (even_register_pair && (!unnamed || words != 2U)))
        return false;
    next = *cursor;
    if (next.variadic_stack_only) next.register_words = 8U;
    if (even_register_pair && !next.variadic_stack_only) {
        if ((next.register_words & 1U) != 0U) ++next.register_words;
        if (next.register_words > 6U) next.register_words = 8U;
    }
    available = 8U - next.register_words;
    loc.register_begin = next.register_words;
    loc.register_count = words < available ? words : available;
    if (even_register_pair && loc.register_count != words)
        loc.register_count = 0U;
    loc.stack_count = words - loc.register_count;
    /* An aligned vararg pair may leave a one-XLEN stack hole. */
    if (even_register_pair && loc.stack_count != 0U &&
        (next.stack_words & 1U) != 0U) {
        if (next.stack_words == SIZE_MAX) return false;
        ++next.stack_words;
    }
    if (next.stack_words > SIZE_MAX - loc.stack_count)
        return false;
    loc.stack_begin = next.stack_words;
    next.register_words += loc.register_count;
    next.stack_words += loc.stack_count;
    if (unnamed && loc.stack_count != 0U) {
        next.variadic_stack_only = true;
        next.register_words = 8U;
    }
    *cursor = next;
    *location = loc;
    return true;
}
#endif
