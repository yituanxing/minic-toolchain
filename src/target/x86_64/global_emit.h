#ifndef MINIC_TARGET_X86_64_GLOBAL_EMIT_H
#define MINIC_TARGET_X86_64_GLOBAL_EMIT_H
#include "core/module_ir.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stdio.h>
/* The x86 ELF writer accepts only sealed ModuleIR global declarations. */
bool minic_x86_64_emit_module_global(FILE *, const MinicTargetInfo *,
                                      const MinicCoreModule *, size_t ordinal);
#endif
