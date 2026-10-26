#ifndef MINIC_TARGET_RISCV64_RECORD_SNAPSHOT_H
#define MINIC_TARGET_RISCV64_RECORD_SNAPSHOT_H

#include "core/core_ir.h"
#include <stdbool.h>

/* RV64-specific, validated call-result/snapshot slot aliasing policy. */
bool minic_riscv64_record_call_result_snapshot_reuse_candidate(
    const MinicCoreFunction *, MinicCoreObjectId, MinicCoreObjectId *);

#endif
