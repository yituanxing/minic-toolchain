#ifndef MINIC_TARGET_RISCV64_CODEGEN_H
#define MINIC_TARGET_RISCV64_CODEGEN_H

#include "core/core_ir.h"
#include "frontend/ast.h"
#include "minic/compiler.h"

#include <stdbool.h>
#include <stddef.h>

bool minic_riscv64_write_c0_program(const char *path,
                                    const MinicC0Program *program,
                                    MinicDiagnostic *diagnostic);

bool minic_riscv64_write_c0_program_with_core_candidates(const char *path,
                                                         const MinicC0Program *program,
                                                         const MinicCoreFunction *core_functions,
                                                         size_t core_function_count,
                                                         MinicDiagnostic *diagnostic);

#endif
