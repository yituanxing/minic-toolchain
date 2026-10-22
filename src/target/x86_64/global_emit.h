#ifndef MINIC_TARGET_X86_64_GLOBAL_EMIT_H
#define MINIC_TARGET_X86_64_GLOBAL_EMIT_H
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stdio.h>
/* Target-owned x86 SysV globals; machine lowering stays separate. */
bool minic_x86_64_emit_global(FILE *, const MinicC0Program *,
                              const MinicTargetInfo *, const MinicGlobalObject *);
#endif
