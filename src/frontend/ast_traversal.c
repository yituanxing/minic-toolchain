#include "frontend/ast_traversal.h"

typedef struct MinicExpressionIdRemapContext {
    const MinicExpressionId *mapping;
    size_t mapping_count;
} MinicExpressionIdRemapContext;

static bool visit_expression_id(MinicExpressionId *expression_id,
                                MinicExpressionIdRefVisitor visitor,
                                void *context) {
    return expression_id != NULL && visitor != NULL && visitor(expression_id, context);
}

bool minic_c0_expression_visit_child_id_refs(MinicExpression *expression,
                                             MinicExpressionIdRefVisitor visitor,
                                             void *context) {
    size_t argument_index;

    if (expression == NULL || visitor == NULL) {
        return false;
    }

    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
    case MINIC_EXPRESSION_FLOATING:
    case MINIC_EXPRESSION_LOCAL:
    case MINIC_EXPRESSION_GLOBAL_OBJECT:
    case MINIC_EXPRESSION_FIXED_REGISTER:
    case MINIC_EXPRESSION_FUNCTION:
    case MINIC_EXPRESSION_LABEL_ADDRESS:
    case MINIC_EXPRESSION_CALL_FRAME_ADDRESS:
    case MINIC_EXPRESSION_BUILTIN_UNREACHABLE:
    case MINIC_EXPRESSION_SIZEOF:
    case MINIC_EXPRESSION_OFFSETOF:
    case MINIC_EXPRESSION_COMPOUND_LITERAL:
        return true;

    case MINIC_EXPRESSION_ADDRESS_OF:
    case MINIC_EXPRESSION_DEREFERENCE:
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_DISCARD:
    case MINIC_EXPRESSION_LVALUE_READ:
    case MINIC_EXPRESSION_UNARY:
        return visit_expression_id(&expression->value.unary.operand, visitor, context);

    case MINIC_EXPRESSION_SUBSCRIPT:
        return visit_expression_id(&expression->value.subscript.base, visitor, context) &&
               visit_expression_id(&expression->value.subscript.index, visitor, context);

    case MINIC_EXPRESSION_MEMBER:
        return visit_expression_id(&expression->value.member.base, visitor, context);

    case MINIC_EXPRESSION_ASSIGNMENT:
    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT:
    case MINIC_EXPRESSION_BINARY:
        return visit_expression_id(&expression->value.binary.left, visitor, context) &&
               visit_expression_id(&expression->value.binary.right, visitor, context);

    case MINIC_EXPRESSION_CONDITIONAL:
        return visit_expression_id(&expression->value.conditional.condition, visitor, context) &&
               visit_expression_id(&expression->value.conditional.when_true, visitor, context) &&
               visit_expression_id(&expression->value.conditional.when_false, visitor, context);

    case MINIC_EXPRESSION_CALL:
        if (expression->value.call.argument_count > MINIC_MAX_FUNCTION_PARAMETERS) {
            return false;
        }
        if (expression->value.call.function_id == MINIC_FUNCTION_INVALID &&
            !visit_expression_id(&expression->value.call.callee, visitor, context)) {
            return false;
        }
        for (argument_index = 0U; argument_index < expression->value.call.argument_count;
             ++argument_index) {
            if (!visit_expression_id(
                    &expression->value.call.arguments[argument_index], visitor, context)) {
                return false;
            }
        }
        return true;

    case MINIC_EXPRESSION_STATEMENT:
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return true;
        }
        return visit_expression_id(
            &expression->value.statement_expression.result, visitor, context);

    case MINIC_EXPRESSION_BUILTIN_UNARY:
        return visit_expression_id(&expression->value.builtin_unary.operand, visitor, context);

    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:
        return visit_expression_id(&expression->value.overflow.left, visitor, context) &&
               visit_expression_id(&expression->value.overflow.right, visitor, context) &&
               visit_expression_id(&expression->value.overflow.result_pointer, visitor, context);
    }

    return false;
}

static bool external_expression_ref_storage_is_valid(const MinicC0Program *program) {
    size_t index;

    if (program == NULL || program->statement_count > program->statement_capacity ||
        program->inline_asm_count > program->inline_asm_capacity ||
        program->cleanup_context_count > program->cleanup_context_capacity ||
        (program->statement_count != 0U && program->statements == NULL) ||
        (program->inline_asm_count != 0U && program->inline_asms == NULL) ||
        (program->cleanup_context_count != 0U && program->cleanup_contexts == NULL)) {
        return false;
    }

    for (index = 0U; index < program->inline_asm_count; ++index) {
        const MinicInlineAsm *inline_asm;

        inline_asm = &program->inline_asms[index];
        if (inline_asm->output_count > inline_asm->output_capacity ||
            inline_asm->input_count > inline_asm->input_capacity ||
            (inline_asm->output_count != 0U && inline_asm->outputs == NULL) ||
            (inline_asm->input_count != 0U && inline_asm->inputs == NULL)) {
            return false;
        }
    }
    return true;
}

static bool visit_external_expression_id_refs_unchecked(MinicC0Program *program,
                                                        MinicExpressionIdRefVisitor visitor,
                                                        void *context) {
    size_t index;

    for (index = 0U; index < program->statement_count; ++index) {
        MinicStatement *statement;

        statement = &program->statements[index];
        if (statement->target_expression != MINIC_EXPRESSION_INVALID &&
            !visit_expression_id(&statement->target_expression, visitor, context)) {
            return false;
        }
        if (statement->expression != MINIC_EXPRESSION_INVALID &&
            !visit_expression_id(&statement->expression, visitor, context)) {
            return false;
        }
    }

    for (index = 0U; index < program->inline_asm_count; ++index) {
        MinicInlineAsm *inline_asm;
        size_t operand_index;

        inline_asm = &program->inline_asms[index];
        for (operand_index = 0U; operand_index < inline_asm->output_count; ++operand_index) {
            if (!visit_expression_id(
                    &inline_asm->outputs[operand_index].expression, visitor, context)) {
                return false;
            }
        }
        for (operand_index = 0U; operand_index < inline_asm->input_count; ++operand_index) {
            if (!visit_expression_id(
                    &inline_asm->inputs[operand_index].expression, visitor, context)) {
                return false;
            }
        }
    }

    for (index = 0U; index < program->cleanup_context_count; ++index) {
        if (!visit_expression_id(
                &program->cleanup_contexts[index].cleanup_expression, visitor, context)) {
            return false;
        }
    }

    if (program->return_expression != MINIC_EXPRESSION_INVALID &&
        !visit_expression_id(&program->return_expression, visitor, context)) {
        return false;
    }

    return true;
}

bool minic_c0_program_visit_external_expression_id_refs(MinicC0Program *program,
                                                        MinicExpressionIdRefVisitor visitor,
                                                        void *context) {
    return visitor != NULL && external_expression_ref_storage_is_valid(program) &&
           visit_external_expression_id_refs_unchecked(program, visitor, context);
}

static bool validate_expression_id_remap(MinicExpressionId *expression_id, void *opaque_context) {
    MinicExpressionIdRemapContext *context;

    if (expression_id == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicExpressionIdRemapContext *)opaque_context;
    return *expression_id < context->mapping_count && context->mapping != NULL &&
           context->mapping[*expression_id] != MINIC_EXPRESSION_INVALID;
}

static bool apply_expression_id_remap(MinicExpressionId *expression_id, void *opaque_context) {
    const MinicExpressionIdRemapContext *context;

    context = (const MinicExpressionIdRemapContext *)opaque_context;
    *expression_id = context->mapping[*expression_id];
    return true;
}

bool minic_c0_program_remap_external_expression_ids(MinicC0Program *program,
                                                    const MinicExpressionId *mapping,
                                                    size_t mapping_count) {
    MinicExpressionIdRemapContext context;

    if (!external_expression_ref_storage_is_valid(program) ||
        (mapping_count != 0U && mapping == NULL)) {
        return false;
    }

    context.mapping = mapping;
    context.mapping_count = mapping_count;
    if (!visit_external_expression_id_refs_unchecked(
            program, validate_expression_id_remap, &context)) {
        return false;
    }

    return visit_external_expression_id_refs_unchecked(program, apply_expression_id_remap, &context);
}
