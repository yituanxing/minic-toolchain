#ifndef MINIC_TARGET_COMMON_LP64_FRAME_H
#define MINIC_TARGET_COMMON_LP64_FRAME_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
/* Bounded simple frame plan, not a universal ABI or an RV64/x86 policy. */
bool minic_lp64_frame_objects(const MinicC0Program *, const MinicTargetInfo *,
                               const MinicCoreFunction *, size_t, size_t *, size_t *);
bool minic_lp64_binary128_slot(const MinicCoreFunction *, size_t, MinicCoreValueId, size_t *);
/* Direct LP64 frame finalization (no ISA instructions, no RV64/x86 policy). */
typedef enum MinicLP64DirectFrameABI {
    MINIC_LP64_FRAME_AAPCS64,
    MINIC_LP64_FRAME_LOONGARCH64_LP64D
} MinicLP64DirectFrameABI;

bool minic_lp64_finalize_direct_frame(
    size_t object_end, bool has_calls, bool dynamic_frame, bool has_varargs,
    MinicLP64DirectFrameABI abi, size_t *save_offset, size_t *va_base,
    size_t *frame_size);

#endif
