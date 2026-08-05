#ifndef MINIC_TARGET_RISCV64_CODEGEN_H
#define MINIC_TARGET_RISCV64_CODEGEN_H

#include "frontend/ast.h"
#include "minic/compiler.h"

#include <stdbool.h>

bool minic_riscv64_write_c0_program(
    const char *path,
    const MinicC0Program *program,
    MinicDiagnostic *diagnostic);

#endif
