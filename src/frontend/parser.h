#ifndef MINIC_FRONTEND_PARSER_H
#define MINIC_FRONTEND_PARSER_H

#include "frontend/ast.h"
#include "minic/compiler.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>

/* A compile must use the same selected target from parsing through codegen. */
bool minic_parse_c0_program_for_target(const char *path,
                                       const char *source,
                                       size_t length,
                                       const MinicTargetInfo *target_info,
                                       MinicC0Program *program,
                                       MinicDiagnostic *diagnostic);

bool minic_parse_c0_program(const char *path,
                            const char *source,
                            size_t length,
                            MinicC0Program *program,
                            MinicDiagnostic *diagnostic);

#endif
