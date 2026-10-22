#ifndef MINIC_TARGET_RISCV64_FRAME_ACCESS_H
#define MINIC_TARGET_RISCV64_FRAME_ACCESS_H
#include "target/riscv64/frame_layout.h"
#include <stdbool.h>
#include <stdio.h>

/* Consumer-only native addressing and SSA value-slot transport. */
bool minic_riscv64_frame_emit_address(FILE *, const MinicRiscv64CoreFrame *, const char *, size_t);
bool minic_riscv64_frame_emit_load64(FILE *, const MinicRiscv64CoreFrame *, const char *, size_t);
bool minic_riscv64_frame_emit_store64(FILE *, const MinicRiscv64CoreFrame *, const char *, size_t);
bool minic_riscv64_frame_load_value(FILE *, const MinicRiscv64CoreFrame *, MinicCoreValueId, const char *);
bool minic_riscv64_frame_store_value(FILE *, const MinicRiscv64CoreFrame *, MinicCoreValueId, const char *);
bool minic_riscv64_frame_load_int128(FILE *, const MinicRiscv64CoreFrame *,
                                     MinicCoreValueId, const char *, const char *);
bool minic_riscv64_frame_store_int128(FILE *, const MinicRiscv64CoreFrame *,
                                      MinicCoreValueId, const char *, const char *);
#endif
