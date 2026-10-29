#ifndef MINIC_TARGET_LOONGARCH64_FRAME_EMIT_H
#define MINIC_TARGET_LOONGARCH64_FRAME_EMIT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
/* LP64D native SP/s0 addressing. $t3 is reserved for large offsets. */
#define MINIC_LARGE_FRAME_MAX 1048576U
bool minic_loongarch64_emit_sp_mem(FILE *, const char *, const char *, size_t);
bool minic_loongarch64_emit_sp_adjust(FILE *, size_t, bool);
bool minic_loongarch64_emit_frame_mem(FILE *, const char *, const char *, size_t, bool);
/* Access a true incoming stack argument through the saved entry SP when
 * a 32B+ local caused the fixed local-frame base to be realigned. */
bool minic_loongarch64_emit_incoming_stack_mem(FILE *, const char *,
    const char *, size_t incoming_offset, size_t saved_entry_sp_offset);
#endif
