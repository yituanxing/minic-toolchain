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
#endif
