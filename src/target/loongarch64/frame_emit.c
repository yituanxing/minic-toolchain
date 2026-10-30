#include "target/loongarch64/frame_emit.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* LDPTR/STPTR use a signed si14 byte offset with two implicit low zero
 * bits: [-32768, 32764] in steps of four.  For positive, naturally aligned
 * frame offsets they are a single instruction where ordinary LD/ST would
 * require a materialized address.  LDPTR.W sign-extends just like LD.W;
 * there is no LDPTR.WU, nor a byte/halfword/FP counterpart.  Keep all other
 * forms on the existing conservative address-materialization path. */
static const char *loongarch64_frame_ptr_op(const char *op, size_t offset) {
    if (op == NULL || offset <= 2047U || offset > 32764U ||
        (offset & 3U) != 0U)
        return NULL;
    if (strcmp(op, "ld.d") == 0 && (offset & 7U) == 0U) return "ldptr.d";
    if (strcmp(op, "st.d") == 0 && (offset & 7U) == 0U) return "stptr.d";
    if (strcmp(op, "ld.w") == 0) return "ldptr.w";
    if (strcmp(op, "st.w") == 0) return "stptr.w";
    return NULL;
}

static bool loongarch64_emit_base_mem(FILE *out, const char *op,
                                     const char *reg, const char *base,
                                     size_t offset) {
    const char *ptr_op;
    if (out == NULL || op == NULL || reg == NULL || base == NULL)
        return false;
    if (offset <= 2047U)
        return fprintf(out, "  %s %s, %s, %zu\n", op, reg, base, offset) >= 0;
    if (offset > MINIC_LARGE_FRAME_MAX) return false;
    ptr_op = loongarch64_frame_ptr_op(op, offset);
    if (ptr_op != NULL)
        return fprintf(out, "  %s %s, %s, %zu\n",
                       ptr_op, reg, base, offset) >= 0;
    return fprintf(out, "  li.d $t3, %zu\n"
                        "  add.d $t3, %s, $t3\n"
                        "  %s %s, $t3, 0\n", offset, base, op, reg) >= 0;
}

/* Large frame offsets and SP adjustment use $t3 as an address scratch.
 * In the large-offset store case, reg must not be $t3 (the address setup
 * would overwrite its payload). Current callers use separate data registers;
 * the restriction is a helper precondition, not a proven emitted-code bug. */
bool minic_loongarch64_emit_sp_mem(FILE *out, const char *op, const char *reg,
                        size_t offset) {
    return loongarch64_emit_base_mem(out, op, reg, "$sp", offset);
}

bool minic_loongarch64_emit_sp_adjust(FILE *out, size_t bytes, bool restore) {
    if (bytes == 0U) return true;
    if (bytes > MINIC_LARGE_FRAME_MAX) return false;
    /* addi.d has a signed 12-bit byte immediate.  Most functions have a
     * small aligned frame; avoid constructing an integer in $t3 at both
     * entry and exit.  -2048 is representable, +2048 is not. */
    if ((!restore && bytes <= 2048U) || (restore && bytes <= 2047U))
        return restore
            ? fprintf(out, "  addi.d $sp, $sp, %zu\n", bytes) >= 0
            : fprintf(out, "  addi.d $sp, $sp, -%zu\n", bytes) >= 0;
    return fprintf(out, "  li.d $t3, %zu\n"
                        "  %s $sp, $sp, $t3\n", bytes,
                   restore ? "add.d" : "sub.d") >= 0;
}

/* Producing a frame-object address is distinct from performing a memory
 * access: expose it once for record ABI transport and address-taken locals.
 * Avoid two-instruction LI+ADD when signed imm12 can address the object. */
bool minic_loongarch64_emit_frame_address(FILE *out, const char *reg,
    size_t offset, bool dynamic_frame) {
    const char *base = dynamic_frame ? "$s0" : "$sp";
    if (out == NULL || reg == NULL || offset > MINIC_LARGE_FRAME_MAX)
        return false;
    if (offset <= 2047U)
        return fprintf(out, "  addi.d %s, %s, %zu\n",
                       reg, base, offset) >= 0;
    return fprintf(out, "  li.d %s, %zu\n"
                        "  add.d %s, %s, %s\n",
                   reg, offset, reg, base, reg) >= 0;
}

bool minic_loongarch64_emit_frame_mem(FILE *out, const char *op, const char *reg,
                           size_t offset, bool dynamic_frame) {
    return loongarch64_emit_base_mem(
        out, op, reg, dynamic_frame ? "$s0" : "$sp", offset);
}

bool minic_loongarch64_emit_incoming_stack_mem(
    FILE *out, const char *op, const char *reg,
    size_t incoming_offset, size_t saved_entry_sp_offset) {
    if (out == NULL || op == NULL || reg == NULL ||
        saved_entry_sp_offset == SIZE_MAX ||
        incoming_offset > MINIC_LARGE_FRAME_MAX ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t3", saved_entry_sp_offset, true))
        return false;
    if (incoming_offset <= 2047U)
        return fprintf(out, "  %s %s, $t3, %zu\n",
                       op, reg, incoming_offset) >= 0;
    return fprintf(out, "  li.d $t4, %zu\n"
                        "  add.d $t3, $t3, $t4\n"
                        "  %s %s, $t3, 0\n",
                   incoming_offset, op, reg) >= 0;
}
