#ifndef MINIC_TARGET_AARCH64_CODEGEN_H
#define MINIC_TARGET_AARCH64_CODEGEN_H

#include "core/core_ir.h"
#include "frontend/ast.h"
#include "minic/compiler.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct MinicCoreModule MinicCoreModule;

/* Real M1 narrow integer Core -> AArch64 GNU ELF emitter.
 * Unsupported operations never silently become another target's code. */
bool minic_aarch64_write_c0_program_with_core_module(
    const char *path,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    const MinicCoreModule *module, MinicDiagnostic *diagnostic);

#endif
