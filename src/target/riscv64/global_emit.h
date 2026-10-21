#ifndef MINIC_TARGET_RISCV64_GLOBAL_EMIT_H
#define MINIC_TARGET_RISCV64_GLOBAL_EMIT_H
#include "frontend/ast.h"
#include <stdbool.h>
#include <stdio.h>
/* RV64 aggregate, bit-field, GNU relocation and file asm owner. */
bool minic_riscv64_emit_global_object(FILE *, const MinicC0Program *, const MinicGlobalObject *);
bool minic_riscv64_emit_file_asm(FILE *, const MinicFileAsm *);
#endif
