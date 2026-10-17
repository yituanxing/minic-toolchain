#!/usr/bin/env python3
"""Disposable AST Expression one-factor compact CALL payload experiment.

Store 8 (rather than 64) 32-bit IDs inline in every Expression union.
Only calls with >8 arguments get a private overflow allocation on insertion;
parser uses stack-only scratch, and normalization remaps via independent
stack scratch before the add_expression deep copy. Existing program/copies
retain value-only semantics without sharing mutable overflow arrays.

CAUTION: benchmark-only; no production C is changed until tested 500/2064.
"""
from pathlib import Path
import re

INLINE=8
def modify(path,changes):
    p=Path(path);s=p.read_text()
    for old,new in changes:
        cnt=s.count(old)
        if cnt!=1:
            raise SystemExit("MINIC_CALL_INLINE8_ANCHOR_MISMATCH %s count=%s snippet=%r"%
                             (path,cnt,old[:110]))
        s=s.replace(old,new,1)
    p.write_text(s)

modify("src/frontend/ast.h",[
    ("            uint32_t arguments[MINIC_MAX_FUNCTION_PARAMETERS];",
     """            uint32_t arguments[8];
            /* Program-owned private array of at most 56 overflow IDs;
               temporary parser builders borrow stack scratch instead. */
            uint32_t *overflow_arguments;"""),
    ("""/* Inline 32-bit argument IDs preserve value-copy ownership of AST nodes.
 * Program::add_expression rejects IDs that cannot fit in these slots. */
static inline bool minic_c0_call_set_argument(MinicExpression *expression,
                                              size_t index,
                                              MinicExpressionId id) {
    if (expression == NULL || index >= MINIC_MAX_FUNCTION_PARAMETERS ||
        id > UINT32_MAX) {
        return false;
    }
    expression->value.call.arguments[index] = (uint32_t)id;
    return true;
}""",
     """/* Experimental compact CALL values retain all 64 ID slots, but only the
 * first eight are inside the Expr union. Setter never allocates: producers
 * of >8-arg CALLs must provide scratch for overflow until Program insertion.
 * Program_add_expression deep-copies overflow into private owned storage. */
#define MINIC_CALL_INLINE_ARGUMENTS 8U
static inline MinicExpressionId minic_c0_call_argument(
    const MinicExpression *expression, size_t index) {
    if (expression == NULL || index >= MINIC_MAX_FUNCTION_PARAMETERS) {
        return MINIC_EXPRESSION_INVALID;
    }
    if (index < MINIC_CALL_INLINE_ARGUMENTS) {
        return expression->value.call.arguments[index];
    }
    if (expression->value.call.overflow_arguments == NULL) {
        return MINIC_EXPRESSION_INVALID;
    }
    return expression->value.call.overflow_arguments[index-MINIC_CALL_INLINE_ARGUMENTS];
}
static inline bool minic_c0_call_set_argument(MinicExpression *expression,
                                              size_t index,
                                              MinicExpressionId id) {
    if (expression == NULL || index >= MINIC_MAX_FUNCTION_PARAMETERS ||
        id > UINT32_MAX) {
        return false;
    }
    if (index < MINIC_CALL_INLINE_ARGUMENTS) {
        expression->value.call.arguments[index] = (uint32_t)id;
        return true;
    }
    if (expression->value.call.overflow_arguments == NULL) {
        return false;
    }
    expression->value.call.overflow_arguments[index-MINIC_CALL_INLINE_ARGUMENTS] =
        (uint32_t)id;
    return true;
}""")
])
modify("src/frontend/ast.c",[
    ("""    for (index = 0U; index < program->function_count; ++index) {
        free(program->functions[index].name);""",
     """    for (index = 0U; index < program->expression_count; ++index) {
        if (program->expressions[index].kind == MINIC_EXPRESSION_CALL) {
            free(program->expressions[index].value.call.overflow_arguments);
        }
    }
    for (index = 0U; index < program->function_count; ++index) {
        free(program->functions[index].name);"""),
    ("""bool minic_c0_program_add_expression(MinicC0Program *program,
                                     const MinicExpression *expression,
                                     MinicExpressionId *expression_id) {
    /* Prevent truncating references stored by CALL expressions. */
    if (program->expression_count >= (size_t)UINT32_MAX) {
        return false;
    }
    if (!minic_grow_array((void **)&program->expressions,
                          &program->expression_capacity,
                          program->expression_count,
                          sizeof(*program->expressions))) {
        return false;
    }

    *expression_id = program->expression_count;
    program->expressions[program->expression_count] = *expression;
    program->expression_count += 1U;
    return true;
}""",
     """bool minic_c0_program_add_expression(MinicC0Program *program,
                                     const MinicExpression *expression,
                                     MinicExpressionId *expression_id) {
    uint32_t *owned_overflow = NULL;
    size_t extra_count = 0U;
    /* The argument scratch is borrowed from a parser stack or source AST.
       Deep-copy here BEFORE grow/realloc; never alias or transfer ownership.
       Thus rewrites and cloned nodes cannot free each other's IDs. */
    if (program == NULL || expression == NULL || expression_id == NULL ||
        program->expression_count >= (size_t)UINT32_MAX) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CALL) {
        if (expression->value.call.argument_count > MINIC_MAX_FUNCTION_PARAMETERS) {
            return false;
        }
        if (expression->value.call.argument_count > MINIC_CALL_INLINE_ARGUMENTS) {
            extra_count = expression->value.call.argument_count - MINIC_CALL_INLINE_ARGUMENTS;
            if (expression->value.call.overflow_arguments == NULL ||
                extra_count > SIZE_MAX / sizeof(*owned_overflow)) {
                return false;
            }
            owned_overflow = (uint32_t *)malloc(extra_count * sizeof(*owned_overflow));
            if (owned_overflow == NULL) {
                return false;
            }
            (void)memcpy(owned_overflow, expression->value.call.overflow_arguments,
                         extra_count * sizeof(*owned_overflow));
        }
    }
    if (!minic_grow_array((void **)&program->expressions,
                          &program->expression_capacity,
                          program->expression_count,
                          sizeof(*program->expressions))) {
        free(owned_overflow);
        return false;
    }

    *expression_id = program->expression_count;
    program->expressions[program->expression_count] = *expression;
    if (expression->kind == MINIC_EXPRESSION_CALL) {
        program->expressions[program->expression_count].value.call.overflow_arguments =
            owned_overflow;
    }
    program->expression_count += 1U;
    return true;
}""")
])
modify("src/frontend/parser_expression.c",[
    ("""            MinicSourcePosition call_end;
            const MinicFunction *callee;

            callee = minic_c0_program_function(parser->program, function_id);""",
     """            MinicSourcePosition call_end;
            const MinicFunction *callee;
            uint32_t call_overflow_scratch[MINIC_MAX_FUNCTION_PARAMETERS -
                                           MINIC_CALL_INLINE_ARGUMENTS];

            callee = minic_c0_program_function(parser->program, function_id);"""),
    ("""            expression.value.call.function_id = function_id;
            if (!parse_call_arguments(parser, &expression, callee)) {""",
     """            expression.value.call.function_id = function_id;
            expression.value.call.overflow_arguments = call_overflow_scratch;
            if (!parse_call_arguments(parser, &expression, callee)) {""")
])
modify("src/frontend/parser_postfix.c",[
    ("""    MinicFunctionType function_type_snapshot;
    MinicExpression call;
    MinicSourcePosition call_end;""",
     """    MinicFunctionType function_type_snapshot;
    MinicExpression call;
    uint32_t call_overflow_scratch[MINIC_MAX_FUNCTION_PARAMETERS -
                                   MINIC_CALL_INLINE_ARGUMENTS];
    MinicSourcePosition call_end;"""),
    ("""    call.value.call.function_id = MINIC_FUNCTION_INVALID;
    call.value.call.callee = callee_id;
    if (!parse_indirect_arguments(parser, &call, &function_type_snapshot)) {""",
     """    call.value.call.function_id = MINIC_FUNCTION_INVALID;
    call.value.call.callee = callee_id;
    call.value.call.overflow_arguments = call_overflow_scratch;
    if (!parse_indirect_arguments(parser, &call, &function_type_snapshot)) {""")
])
# Replace strictly reads of CALL IDs. Zero occurrence for parser writers
# (already centralized in minic_c0_call_set_argument).
for path in ("src/frontend/ast_traversal.c",
             "src/frontend/ast_verifier.c","src/compiler/compiler.c"):
    p=Path(path);s=p.read_text()
    pattern=r'\b([A-Za-z_][A-Za-z_0-9]*)->value\.call\.arguments\[([^\]]+)\]'
    s,count=re.subn(pattern,r'minic_c0_call_argument(\1, \2)',s)
    if count==0:raise SystemExit("MINIC_CALL_INLINE8_NO_READ_SITES "+path)
    p.write_text(s)
    print("MINIC_CALL_INLINE8_READ_SITES path=%s replacements=%d"%(path,count))

modify("src/frontend/cast_normalization.c",[
    ("""        } else {
            MinicExpression copied_expression;

            copied_expression = *old_expression;
            success = remap_non_cast_expression(
                          &copied_expression, mapping, old_expression_count, expression_index) &&
                      minic_c0_program_add_expression(&rewritten, &copied_expression, &new_id);
        }""",
     """        } else {
            MinicExpression copied_expression;
            uint32_t private_overflow[MINIC_MAX_FUNCTION_PARAMETERS -
                                      MINIC_CALL_INLINE_ARGUMENTS];

            copied_expression = *old_expression;
            /* Normalization remaps IDs in-place. Never modify the old
               Program's owned overflow through a by-value pointer copy. */
            if (copied_expression.kind == MINIC_EXPRESSION_CALL &&
                copied_expression.value.call.argument_count > MINIC_CALL_INLINE_ARGUMENTS) {
                size_t extra = copied_expression.value.call.argument_count -
                               MINIC_CALL_INLINE_ARGUMENTS;
                if (extra > MINIC_MAX_FUNCTION_PARAMETERS-MINIC_CALL_INLINE_ARGUMENTS ||
                    copied_expression.value.call.overflow_arguments == NULL) {
                    success = false;
                    break;
                }
                (void)memcpy(private_overflow,
                             copied_expression.value.call.overflow_arguments,
                             extra * sizeof(*private_overflow));
                copied_expression.value.call.overflow_arguments = private_overflow;
            }
            success = remap_non_cast_expression(
                          &copied_expression, mapping, old_expression_count, expression_index) &&
                      minic_c0_program_add_expression(&rewritten, &copied_expression, &new_id);
        }"""),
    ("""    if (success) {
        free(program->expressions);
        program->expressions = rewritten.expressions;""",
     """    if (success) {
        /* On commit, release old Program overflow AFTER the complete cloned
           destination has been built. On failure, preserve original owner. */
        for (expression_index = 0U; expression_index < old_expression_count;
             ++expression_index) {
            if (program->expressions[expression_index].kind == MINIC_EXPRESSION_CALL) {
                free(program->expressions[expression_index].value.call.overflow_arguments);
            }
        }
        free(program->expressions);
        program->expressions = rewritten.expressions;"""),
    ("""    free(rewritten.expressions);
    free(mapping);
    return success;""",
     """    /* Only uncommitted rewritten Expressions remain owned by rewritten.
       On successful transfer the buffer and count were reset to NULL/zero. */
    for (expression_index = 0U; expression_index < rewritten.expression_count;
         ++expression_index) {
        if (rewritten.expressions[expression_index].kind == MINIC_EXPRESSION_CALL) {
            free(rewritten.expressions[expression_index].value.call.overflow_arguments);
        }
    }
    free(rewritten.expressions);
    free(mapping);
    return success;""")
])
print("MINIC_CALL_INLINE8_SOURCE=APPLIED inline=8 max=64 overflow=owned_per_CALL_9plus")
