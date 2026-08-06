#ifndef MINIC_TARGET_RISCV64_LAYOUT_H
#define MINIC_TARGET_RISCV64_LAYOUT_H

#include "frontend/ast.h"
#include "minic/compiler.h"

#include <stdbool.h>
#include <stddef.h>

bool minic_riscv64_type_layout(
    const MinicC0Program *program,
    MinicType type,
    size_t *size,
    size_t *alignment);
bool minic_riscv64_layout_program(
    const char *path,
    MinicC0Program *program,
    MinicDiagnostic *diagnostic);

#endif
