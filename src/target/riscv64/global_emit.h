#ifndef MINIC_TARGET_RISCV64_GLOBAL_EMIT_H
#define MINIC_TARGET_RISCV64_GLOBAL_EMIT_H
#include "core/module_ir.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stdio.h>
/* RV64 aggregate, bit-field, GNU relocation and file asm owner. */
bool minic_riscv64_emit_global_object(FILE *, const MinicC0Program *, const MinicGlobalObject *);
/* Production ModuleIR data path. No source Program or C type is consulted. */
bool minic_riscv64_emit_module_global(FILE *, const MinicTargetInfo *,
                                      const MinicCoreModule *, size_t ordinal);
bool minic_riscv64_emit_file_asm(FILE *, const MinicFileAsm *);
bool minic_riscv64_emit_file_asm_bytes(FILE *, const char *, size_t);
#endif
