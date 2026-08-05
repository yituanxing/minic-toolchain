#ifndef MINIC_FRONTEND_PARSER_H
#define MINIC_FRONTEND_PARSER_H

#include "frontend/ast.h"
#include "minic/compiler.h"

#include <stdbool.h>
#include <stddef.h>

bool minic_parse_c0_program(
    const char *path,
    const char *source,
    size_t length,
    MinicC0Program *program,
    MinicDiagnostic *diagnostic);

#endif
