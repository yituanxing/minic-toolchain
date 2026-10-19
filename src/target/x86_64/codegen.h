#ifndef MINIC_TARGET_X86_64_CODEGEN_H
#define MINIC_TARGET_X86_64_CODEGEN_H

#include "core/core_ir.h"
#include "frontend/ast.h"
#include "minic/compiler.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>

/* M1: conservative Core -> GNU x86-64 assembly emitter, Linux SysV.
 * Unsupported Core operations and program constructs must fail explicitly. */
bool minic_x86_64_write_c0_program_with_core_functions(
    const char *path,
    const MinicC0Program *program,
    const MinicTargetInfo *target,
    const MinicCoreFunction *core_functions,
    size_t core_function_count,
    bool function_sections,
    MinicDiagnostic *diagnostic);

#endif
