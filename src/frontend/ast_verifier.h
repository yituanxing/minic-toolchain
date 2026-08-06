#ifndef MINIC_FRONTEND_AST_VERIFIER_H
#define MINIC_FRONTEND_AST_VERIFIER_H

#include "frontend/ast.h"

typedef enum MinicC0AstForm { MINIC_C0_AST_PARSED = 0, MINIC_C0_AST_NORMALIZED } MinicC0AstForm;

bool minic_c0_program_verify(const MinicC0Program *program, MinicC0AstForm form);

#endif
