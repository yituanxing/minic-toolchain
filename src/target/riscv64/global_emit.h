#ifndef MINIC_TARGET_RISCV64_GLOBAL_EMIT_H
#define MINIC_TARGET_RISCV64_GLOBAL_EMIT_H
#include "core/module_ir.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stdio.h>
/* Production ModuleIR data path. No source Program or C type is consulted. */
bool minic_riscv64_emit_module_global(FILE *, const MinicTargetInfo *,
                                      const MinicCoreModule *, size_t ordinal);
/* Raw assembler text is already validated at source/module ingress. */
bool minic_riscv64_emit_file_asm_bytes(FILE *, const char *, size_t);
#endif
