#ifndef MINIC_FRONTEND_AST_VERIFIER_H
#define MINIC_FRONTEND_AST_VERIFIER_H

#include "frontend/ast.h"
#include "target/target_info.h"

typedef enum MinicC0AstForm { MINIC_C0_AST_PARSED = 0, MINIC_C0_AST_NORMALIZED } MinicC0AstForm;

#define MINIC_C0_AST_VERIFY_INDEX_NONE ((size_t)-1)

typedef enum MinicC0AstVerifyStage {
    MINIC_C0_AST_VERIFY_PROGRAM = 0,
    MINIC_C0_AST_VERIFY_ENUM,
    MINIC_C0_AST_VERIFY_ENUMERATOR,
    MINIC_C0_AST_VERIFY_ARRAY_TYPE,
    MINIC_C0_AST_VERIFY_FUNCTION_TYPE,
    MINIC_C0_AST_VERIFY_RECORD,
    MINIC_C0_AST_VERIFY_LOCAL,
    MINIC_C0_AST_VERIFY_FIXED_REGISTER,
    MINIC_C0_AST_VERIFY_FUNCTION,
    MINIC_C0_AST_VERIFY_TYPE_ALIAS,
    MINIC_C0_AST_VERIFY_GLOBAL_OBJECT,
    MINIC_C0_AST_VERIFY_FILE_ASM,
    MINIC_C0_AST_VERIFY_EXPRESSION,
    MINIC_C0_AST_VERIFY_STATEMENT,
    MINIC_C0_AST_VERIFY_BLOCK
} MinicC0AstVerifyStage;

/* Stable failure provenance for compiler diagnostics; indices refer to the
 * owning Program arena and optional nested member/parameter position. */
typedef struct MinicC0AstVerifyFailure {
    MinicC0AstVerifyStage stage;
    size_t index;
    size_t subindex;
    const char *reason;
} MinicC0AstVerifyFailure;

const char *minic_c0_ast_verify_stage_name(MinicC0AstVerifyStage stage);
bool minic_c0_program_verify_target_detailed(const MinicC0Program *program,
                                             MinicC0AstForm form,
                                             const MinicTargetInfo *target,
                                             MinicC0AstVerifyFailure *failure);
bool minic_c0_program_verify_detailed(const MinicC0Program *program,
                                      MinicC0AstForm form,
                                      MinicC0AstVerifyFailure *failure);

bool minic_c0_program_verify_target(const MinicC0Program *program,
                                    MinicC0AstForm form,
                                    const MinicTargetInfo *target);
bool minic_c0_program_verify(const MinicC0Program *program, MinicC0AstForm form);

#endif
