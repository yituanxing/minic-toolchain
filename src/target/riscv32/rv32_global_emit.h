#ifndef MINIC_RISCV_RV32_GLOBAL_EMIT_H
#define MINIC_RISCV_RV32_GLOBAL_EMIT_H

#include "core/module_ir.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stdio.h>

/* RV32 consumes sealed ModuleIR globals and their machine data layout. */
bool minic_riscv32_emit_module_global(FILE *, const MinicTargetInfo *,
                                      const MinicCoreModule *, size_t ordinal);
#endif
