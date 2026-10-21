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
#endif
