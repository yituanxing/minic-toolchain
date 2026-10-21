#include "target/loongarch64/frame_emit.h"
#include <stdio.h>

/* Original LoongArch64 immediate/frame-address encoding. */
bool minic_loongarch64_emit_sp_mem(FILE *out, const char *op, const char *reg,
                        size_t offset) {
    if (offset <= 2047U)
        return fprintf(out, "  %s %s, $sp, %zu\n", op, reg, offset) >= 0;
    if (offset > MINIC_LARGE_FRAME_MAX) return false;
    return fprintf(out, "  li.d $t3, %zu\n"
                        "  add.d $t3, $sp, $t3\n"
                        "  %s %s, $t3, 0\n", offset, op, reg) >= 0;
}

bool minic_loongarch64_emit_sp_adjust(FILE *out, size_t bytes, bool restore) {
    if (bytes == 0U) return true;
    if (bytes > MINIC_LARGE_FRAME_MAX) return false;
    return fprintf(out, "  li.d $t3, %zu\n"
                        "  %s $sp, $sp, $t3\n", bytes,
                   restore ? "add.d" : "sub.d") >= 0;
}

bool minic_loongarch64_emit_frame_mem(FILE *out, const char *op, const char *reg,
                           size_t offset, bool dynamic_frame) {
    if (!dynamic_frame) return minic_loongarch64_emit_sp_mem(out, op, reg, offset);
    if (offset <= 2047U)
        return fprintf(out, "  %s %s, $s0, %zu\n", op, reg, offset) >= 0;
    if (offset > MINIC_LARGE_FRAME_MAX) return false;
    return fprintf(out, "  li.d $t3, %zu\n"
                        "  add.d $t3, $s0, $t3\n"
                        "  %s %s, $t3, 0\n", offset, op, reg) >= 0;
}
