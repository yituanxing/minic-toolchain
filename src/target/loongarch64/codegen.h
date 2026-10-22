#ifndef MINIC_TARGET_LOONGARCH64_CODEGEN_H
#define MINIC_TARGET_LOONGARCH64_CODEGEN_H

#include "core/core_ir.h"
#include "frontend/ast.h"
#include "minic/compiler.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>

/* Real M1 narrow integer Core -> LoongArch64 GNU ELF emitter.
 * Unsupported operations never silently become another target's code. */
bool minic_loongarch64_write_c0_program_with_core_functions(
    const char *path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    MinicDiagnostic *diagnostic);

#endif
