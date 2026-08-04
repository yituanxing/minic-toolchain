#ifndef MINIC_FRONTEND_PARSER_H
#define MINIC_FRONTEND_PARSER_H

#include "minic/compiler.h"

#include <stdbool.h>
#include <stddef.h>

bool minic_parse_c0_translation_unit(
    const char *path,
    const char *source,
    size_t length,
    int *return_value,
    MinicDiagnostic *diagnostic);

#endif
