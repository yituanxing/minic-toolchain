#ifndef MINIC_RISCV_RV32_GLOBAL_EMIT_H
#define MINIC_RISCV_RV32_GLOBAL_EMIT_H

#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stdio.h>

/* RV32 uses the shared target-neutral data layout rather than RV64's
 * LP64-only global emission. Unsupported aggregate relocation models
 * fail closed; scalar and flat byte/half/word tables are real ELF32. */
bool minic_riscv32_emit_flat_global(FILE *out,
                                    const MinicC0Program *program,
                                    const MinicTargetInfo *target,
                                    const MinicGlobalObject *object);
#endif
