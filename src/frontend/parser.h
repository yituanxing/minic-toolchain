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

/*
 * Literal-only compatibility entry point used until the compiler driver
 * switches to the owned AST. It will be removed immediately after cutover.
 * 仅在编译驱动切换到自有 AST 前保留的常量兼容入口，切换后立即删除。
 */
bool minic_parse_c0_translation_unit(
    const char *path,
    const char *source,
    size_t length,
    int *return_value,
    MinicDiagnostic *diagnostic);

#endif
