#ifndef MINIC_TARGET_RISCV64_LAYOUT_H
#define MINIC_TARGET_RISCV64_LAYOUT_H

#include "frontend/ast.h"
#include "minic/compiler.h"

#include <stdbool.h>

bool minic_riscv64_layout_program(
    const char *path,
    MinicC0Program *program,
    MinicDiagnostic *diagnostic);

#endif
