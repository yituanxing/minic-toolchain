#include "core/core_lower_internal.h"
#include "core/local_facts.h"
#include "core/cfg_constant_calls.h"
#include "core/core_const_global_record.h"
#include "core/opt_pure_statement.h"
#include "core/source_loop_shapes.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include "frontend/ast_traversal.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Conservative source-local and CFG proof. No machine ABI or stack state. */
/* M183_TAIL_CFG_CLOSURE: symbolic function-pointer identity in a
 * specialized inline clone.  The recorded symbolic expression is a parsed,
 * immutable static expression; recursively unwrap only representation-preserving
 * casts/reads and parameter facts until a function designator is reached. */
static bool core_cfg_specialized_function_symbol(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    unsigned int depth,
    MinicFunctionId *function_id) {
    const MinicC0Program *program;
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->source_function == NULL || function_id == NULL || depth > 32U) {
        return false;
    }
    program = context->body->program;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        *function_id = expression->value.function_id;
        return *function_id < program->function_count;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return core_cfg_specialized_function_symbol(
            context, expression->value.unary.operand, depth + 1U, function_id);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicFunction *specialized = context->source_function;
        const MinicFunction *source;
        MinicLocalId local_id;
        size_t parameter_index;

        if (!specialized->is_integer_specialization ||
            specialized->specialization_source >= program->function_count) {
            return false;
        }
        source = minic_c0_program_function(program, specialized->specialization_source);
        if (source == NULL) {
            return false;
        }
        local_id = expression->value.local_id;
        if (local_id < source->local_begin) {
            return false;
        }
        parameter_index = local_id - source->local_begin;
        if (parameter_index >= source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !specialized->specialization_symbolic_known[parameter_index] ||
            specialized->specialization_symbolic_expression[parameter_index] ==
                MINIC_EXPRESSION_INVALID) {
            return false;
        }
        return core_cfg_specialized_function_symbol(
            context,
            specialized->specialization_symbolic_expression[parameter_index],
            depth + 1U,
            function_id);
    }
    return false;
}


/* RUNTIME_STABLE_ZERO_BOOLS_V0
   This proof precedes, and never follows, CFG-based propagation. A function
   initializer alone is insufficient: EVERY write must have the same value.
   A label/goto can then join arbitrary predecessors without changing zero. */
static bool core_program_expr_is_literal_zero(
    const MinicCoreLowerContext *context, MinicExpressionId id) {
    MinicConstValue value;
    int64_t number;
    return context != NULL && context->body != NULL &&
           context->body->program != NULL && context->target != NULL &&
           minic_const_eval_integer(context->body->program,
                                    context->target, id, &value) &&
           minic_const_value_as_int64(context->body->program,
                                      context->target, &value, &number) &&
           number == 0;
}

static bool core_program_expr_is_local(
    const MinicC0Program *program, MinicExpressionId expression_id,
    MinicLocalId local_id) {
    const MinicExpression *expression =
        minic_c0_program_expression(program, expression_id);
    return expression != NULL &&
           expression->kind == MINIC_EXPRESSION_LOCAL &&
           expression->value.local_id == local_id;
}

static bool core_program_bool_has_entry_zero_initializer(
    const MinicCoreLowerContext *context, MinicLocalId local_id) {
    const MinicC0Program *program = context->body->program;
    const MinicBlock *entry = minic_c0_program_block(
        program, context->source_function->body_block);
    size_t i;
    if (entry == NULL) {
        return false;
    }
    /* A leading sequence of direct assignments is the only entry region
       accepted. C source declarations initialized later or conditionally
       are NOT credited with dominating every defined use. */
    for (i = 0U; i < entry->statement_count && i < 64U; ++i) {
        const MinicStatement *stmt =
            minic_c0_program_statement(program, entry->statements[i]);
        const MinicExpression *target;
        if (stmt == NULL || stmt->kind != MINIC_STATEMENT_ASSIGN) {
            break;
        }
        target = minic_c0_program_expression(program, stmt->target_expression);
        if (target == NULL || target->kind != MINIC_EXPRESSION_LOCAL) {
            break;
        }
        if (target->value.local_id == local_id) {
            return core_program_expr_is_literal_zero(context, stmt->expression);
        }
    }
    return false;
}

/* Core lower_block owns the canonical external goto/asm-goto reentry
   oracle.  It is defined later in this translation unit. */
bool minic_core_lower_unreachable_external_reentry(
    const MinicCoreLowerContext *context,
    const MinicStatement *root_statement,
    MinicStatementId extra_statement_id);

/* The frontend still retains syntactic stores following a guaranteed
   return.  Core lower_block skips exactly these unreachable tails.  Discount
   such stores from a whole-function invariant proof, but only if there is
   no label/case/default reentry between the terminator and that store. */
static bool core_program_statement_must_return(
    const MinicCoreLowerContext *context,
    const MinicStatement *statement) {
    const MinicC0Program *program;
    const MinicBlock *then_block;
    const MinicStatement *first;
    MinicConstValue constant;
    bool zero;

    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || statement == NULL) {
        return false;
    }
    if (statement->kind == MINIC_STATEMENT_RETURN) {
        return true;
    }
    if (statement->kind != MINIC_STATEMENT_IF ||
        statement->then_block == MINIC_BLOCK_INVALID) {
        return false;
    }
    program = context->body->program;
    if (!minic_const_eval_integer(program, context->target,
                                  statement->expression, &constant) ||
        !minic_const_value_is_zero(program, context->target,
                                   &constant, &zero) || zero) {
        return false;
    }
    then_block = minic_c0_program_block(program, statement->then_block);
    if (then_block == NULL || then_block->statement_count == 0U) {
        return false;
    }
    first = minic_c0_program_statement(program, then_block->statements[0]);
    return first != NULL && first->kind == MINIC_STATEMENT_RETURN;
}

static bool core_program_stmt_is_dead_after_return(
    const MinicCoreLowerContext *context, MinicStatementId statement_id) {
    const MinicC0Program *program;
    size_t block_index;
    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL) {
        return false;
    }
    for (block_index = 0U; block_index < program->block_count; ++block_index) {
        const MinicBlock *block = &program->blocks[block_index];
        size_t i;
        bool must_return = false;
        for (i = 0U; i < block->statement_count; ++i) {
            const MinicStatement *statement =
                minic_c0_program_statement(program, block->statements[i]);
            if (statement == NULL) {
                return false;
            }
            if (block->statements[i] == statement_id) {
                return must_return &&
                    !minic_core_lower_unreachable_external_reentry(
                        context, statement, MINIC_STATEMENT_INVALID);
            }
            if (statement->kind == MINIC_STATEMENT_LABEL ||
                statement->kind == MINIC_STATEMENT_CASE ||
                statement->kind == MINIC_STATEMENT_DEFAULT) {
                must_return = false;
            } else if (core_program_statement_must_return(context, statement)) {
                must_return = true;
            }
        }
    }
    return false;
}

/* Parser may encode an ordinary "x = 1;" as a statement-expression
   containing MINIC_EXPRESSION_ASSIGNMENT rather than MINIC_STATEMENT_ASSIGN.
   The same proven unreachable-tail rule must apply to that exact statement.
   Do not generalize to assignments nested in potentially live expressions. */
static bool core_program_expr_is_dead_after_return(
    const MinicCoreLowerContext *context, MinicExpressionId expression_id) {
    const MinicC0Program *program;
    size_t i;
    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL) {
        return false;
    }
    for (i = 0U; i < program->statement_count; ++i) {
        const MinicStatement *stmt = &program->statements[i];
        if (stmt->kind != MINIC_STATEMENT_EXPRESSION ||
            stmt->expression != expression_id) {
            continue;
        }
        return core_program_stmt_is_dead_after_return(
            context, (MinicStatementId)i);
    }
    return false;
}

static bool core_program_bool_is_provably_zero(
    const MinicCoreLowerContext *context, MinicLocalId local_id) {
    const MinicC0Program *program;
    const MinicLocal *local;
    size_t i;
    bool trace;

    if (context == NULL || context->source_function == NULL ||
        context->body == NULL || context->body->program == NULL ||
        context->target == NULL) {
        return false;
    }
    program = context->body->program;
    trace = getenv("MINIC_DMA_ZERO_TRACE") != NULL &&
            context->source_function->name != NULL &&
            strcmp(context->source_function->name, "dma_direct_alloc") == 0;
    local = minic_c0_program_local(program, local_id);
    if (local == NULL || local->is_array || local->is_register_storage ||
        !minic_type_is_bool_integer(local->type) ||
        minic_type_is_volatile(local->type) ||
        !core_program_bool_has_entry_zero_initializer(context, local_id)) {
        return false;
    }
    for (i = 0U; i < program->statement_count; ++i) {
        const MinicStatement *stmt = &program->statements[i];
        if (!core_program_expr_is_local(
                program, stmt->target_expression, local_id)) {
            continue;
        }
        if (stmt->kind != MINIC_STATEMENT_ASSIGN ||
            (!core_program_expr_is_literal_zero(context, stmt->expression) &&
             !core_program_stmt_is_dead_after_return(
                 context, (MinicStatementId)i))) {
            if (trace) (void)fprintf(stderr,
                "DMA_ZERO_REJECT local=%zu reason=stmt index=%zu line=%zu kind=%d zero=%d dead=%d\\n",
                (size_t)local_id, i, stmt->span.begin.line, (int)stmt->kind,
                core_program_expr_is_literal_zero(context, stmt->expression) ? 1 : 0,
                core_program_stmt_is_dead_after_return(context, (MinicStatementId)i) ? 1 : 0);
            return false;
        }
    }
    for (i = 0U; i < program->expression_count; ++i) {
        const MinicExpression *expr = &program->expressions[i];
        const MinicExpression *lhs;
        switch (expr->kind) {
            case MINIC_EXPRESSION_ADDRESS_OF:
                if (core_program_expr_is_local(
                        program, expr->value.unary.operand, local_id)) {
                    if (trace) (void)fprintf(stderr, "DMA_ZERO_REJECT local=%zu reason=address expr=%zu line=%zu\\n", (size_t)local_id, i, expr->span.begin.line);
                    return false;
                }
                break;
            case MINIC_EXPRESSION_UNARY:
                if ((expr->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
                     expr->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
                     expr->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT ||
                     expr->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT) &&
                    core_program_expr_is_local(
                        program, expr->value.unary.operand, local_id)) {
                    if (trace) (void)fprintf(stderr, "DMA_ZERO_REJECT local=%zu reason=update expr=%zu line=%zu\\n", (size_t)local_id, i, expr->span.begin.line);
                    return false;
                }
                break;
            case MINIC_EXPRESSION_ASSIGNMENT:
            case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT:
                lhs = minic_c0_program_expression(
                    program, expr->value.binary.left);
                if (lhs == NULL || lhs->kind != MINIC_EXPRESSION_LOCAL ||
                    lhs->value.local_id != local_id) {
                    break;
                }
                if (expr->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT ||
                    (!core_program_expr_is_literal_zero(
                         context, expr->value.binary.right) &&
                     !core_program_expr_is_dead_after_return(
                         context, (MinicExpressionId)i))) {
                    if (trace) (void)fprintf(stderr, "DMA_ZERO_REJECT local=%zu reason=expression expr=%zu line=%zu kind=%d\\n", (size_t)local_id, i, expr->span.begin.line, (int)expr->kind);
                    return false;
                }
                break;
            default:
                break;
        }
    }
    for (i = 0U; i < program->inline_asm_count; ++i) {
        const MinicInlineAsm *asm_stmt = &program->inline_asms[i];
        size_t j;
        for (j = 0U; j < asm_stmt->output_count; ++j) {
            if (core_program_expr_is_local(
                    program, asm_stmt->outputs[j].expression, local_id)) {
                if (trace) (void)fprintf(stderr, "DMA_ZERO_REJECT local=%zu reason=asm idx=%zu\\n", (size_t)local_id, i);
                return false;
            }
        }
    }
    return true;
}

void core_local_constant_set(MinicCoreLowerContext *context,
                                    MinicLocalId local_id,
                                    const MinicConstValue *value) {
    const MinicLocal *local;
    MinicConstValue converted;
    MinicType local_type;
    size_t index;

    if (value == NULL || context == NULL || context->body == NULL ||
        context->body->program == NULL || context->target == NULL ||
        !minic_core_local_fact_index(context, local_id, &index) ||
        context->local_integer_constants[index].escaped) {
        return;
    }
    local = minic_c0_program_local(context->body->program, local_id);
    if (local == NULL || local->is_array || minic_type_is_volatile(local->type) ||
        !minic_type_unqualified(local->type, &local_type) ||
        !minic_type_is_integer(local_type) ||
        !minic_const_value_convert_integer(context->body->program,
                                           context->target,
                                           value,
                                           local_type,
                                           &converted)) {
        context->local_integer_constants[index].known = false;
        return;
    }
    context->local_integer_constants[index].value = converted;
    context->local_integer_constants[index].known = true;
    if (getenv("MINIC_SHMEM_IF_TRACE") != NULL &&
        context->source_function != NULL &&
        context->source_function->name != NULL &&
        strcmp(context->source_function->name, "shmem_alloc_and_acct_folio") == 0) {
        (void)fprintf(stderr, "SHMEM_FACT_EVENT event=set local=%zu bits=%llu escaped=%d\n",
            (size_t)local_id,
            (unsigned long long)converted.bits,
            context->local_integer_constants[index].escaped ? 1 : 0);
    }
    if (!context->local_integer_constants[index].program_zero_checked) {
        context->local_integer_constants[index].program_zero_checked = true;
        if (converted.bits == 0U &&
            core_program_bool_is_provably_zero(context, local_id)) {
            context->local_integer_constants[index].program_zero_stable = true;
        }
        if (getenv("MINIC_DMA_ZERO_TRACE") != NULL &&
            context->source_function != NULL &&
            context->source_function->name != NULL &&
            strcmp(context->source_function->name, "dma_direct_alloc") == 0) {
            (void)fprintf(stderr,
                          "DMA_ZERO_PROOF local=%zu line=%zu bits=%llu entry=%d stable=%d\n",
                          (size_t)local_id,
                          (size_t)local->name_span.begin.line,
                          (unsigned long long)converted.bits,
                          core_program_bool_has_entry_zero_initializer(context, local_id) ? 1 : 0,
                          context->local_integer_constants[index].program_zero_stable ? 1 : 0);
        }
    }
}

bool minic_core_lower_direct_local_read(const MinicCoreLowerContext *context,
                                   MinicExpressionId expression_id,
                                   MinicLocalId *local_id) {
    const MinicExpression *expression;
    const MinicExpression *operand;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        local_id == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        *local_id = expression->value.local_id;
        return true;
    }
    if (expression->kind != MINIC_EXPRESSION_LVALUE_READ) {
        return false;
    }
    operand = minic_c0_program_expression(
        context->body->program, expression->value.unary.operand);
    if (operand == NULL || operand->kind != MINIC_EXPRESSION_LOCAL ||
        operand->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    *local_id = operand->value.local_id;
    return true;
}

static bool core_expression_known_null_pointer_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    unsigned int depth) {
    const MinicExpression *expression;
    MinicLocalId local_id;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        depth > 8U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (minic_c0_expression_is_null_pointer_constant_v0(
            context->body->program, expression_id)) {
        return true;
    }
    if ((expression->kind == MINIC_EXPRESSION_CAST ||
         expression->kind == MINIC_EXPRESSION_BITCAST ||
         expression->kind == MINIC_EXPRESSION_CONVERSION) &&
        minic_type_is_pointer(expression->type)) {
        return core_expression_known_null_pointer_depth(
            context, expression->value.unary.operand, depth + 1U);
    }
    if (minic_core_cfg_pure_call_argument(context, expression_id, 0U) &&
        minic_core_lower_direct_local_read(context, expression_id, &local_id) &&
        minic_core_local_null_pointer_get(context, local_id)) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_CALL &&
        expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
        const MinicFunction *callee;
        const MinicBlock *callee_body;
        const MinicStatement *return_statement;
        size_t argument_index;

        /* The returned pointer may be known without the call being elidable.
         * In particular, every argument is evaluated before a helper's
         * unconditional null return. Never discard an effectful argument. */
        for (argument_index = 0U;
             argument_index < expression->value.call.argument_count;
             ++argument_index) {
            if (!minic_core_cfg_pure_call_argument(
                    context,
                    minic_c0_call_argument(expression, argument_index), 0U)) {
                return false;
            }
        }

        callee = minic_c0_program_function(
            context->body->program, expression->value.call.function_id);
        if (callee == NULL || !callee->is_defined || !minic_type_is_pointer(callee->return_type)) {
            return false;
        }
        callee_body = minic_c0_program_block(context->body->program, callee->body_block);
        /* M185_NULL_HELPER_FIRST_RETURN: the first top-level unconditional
           return terminates the function; parser-added fallback returns after
           it are unreachable and must not defeat CFG-only null propagation. */
        if (callee_body == NULL || callee_body->statement_count == 0U) {
            return false;
        }
        return_statement = minic_c0_program_statement(
            context->body->program, callee_body->statements[0]);
        if (return_statement == NULL || return_statement->kind != MINIC_STATEMENT_RETURN ||
            return_statement->expression == MINIC_EXPRESSION_INVALID) {
            return false;
        }
        /* The callee body belongs to another local-fact namespace. A caller's
         * local NULL fact must not prove anything about its return expression.
         * Only an independently pure, literal-derived return is safe. */
        if (!minic_core_cfg_pure_call_argument(
                context, return_statement->expression, 0U) ||
            !minic_c0_expression_is_null_pointer_constant_v0(
                context->body->program, return_statement->expression)) {
            return false;
        }
        return core_expression_known_null_pointer_depth(
            context, return_statement->expression, depth + 1U);
    }
    return false;
}

bool minic_core_lower_known_null_pointer(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id) {
    return core_expression_known_null_pointer_depth(context, expression_id, 0U);
}

bool core_const_signed_value(const MinicCoreLowerContext *context,
                                    const MinicConstValue *value,
                                    int64_t *result) {
    unsigned int width;
    uint64_t bits;
    uint64_t mask;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || value == NULL || result == NULL ||
        !minic_type_is_signed_integer(value->type) ||
        !minic_target_info_integer_width(
            context->target, context->body->program, value->type, &width) ||
        width == 0U || width > 64U) {
        return false;
    }
    bits = value->bits;
    if (width < 64U) {
        mask = (UINT64_C(1) << width) - UINT64_C(1);
        bits &= mask;
        if ((bits & (UINT64_C(1) << (width - 1U))) != 0U) {
            bits |= ~mask;
        }
    }
    (void)memcpy(result, &bits, sizeof(*result));
    return true;
}



bool minic_core_lower_boolean_range_expression(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    unsigned int depth) {
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        depth > 32U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (minic_type_is_bool_integer(expression->type)) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return minic_core_lower_boolean_range_expression(
            context, expression->value.unary.operand, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL &&
        context->source_function != NULL &&
        context->source_function->is_integer_specialization &&
        context->source_function->specialization_source <
            context->body->program->function_count) {
        const MinicFunction *source = &context->body->program->functions[
            context->source_function->specialization_source];
        MinicLocalId local_id = expression->value.local_id;
        size_t parameter_index;

        if (local_id < source->local_begin) {
            return false;
        }
        parameter_index = local_id - source->local_begin;
        return parameter_index < source->parameter_count &&
               parameter_index < MINIC_MAX_FUNCTION_PARAMETERS &&
               context->source_function->specialization_boolean_range_known[
                   parameter_index];
    }
    return false;
}

static bool core_boolean_compare_values(
    const MinicCoreLowerContext *context,
    MinicBinaryOperator operator_kind,
    MinicType common_type,
    const MinicConstValue *left,
    const MinicConstValue *right,
    bool *predicate) {
    if (context == NULL || left == NULL || right == NULL || predicate == NULL) {
        return false;
    }
    if (operator_kind == MINIC_BINARY_EQUAL ||
        operator_kind == MINIC_BINARY_NOT_EQUAL) {
        *predicate = left->bits == right->bits;
        if (operator_kind == MINIC_BINARY_NOT_EQUAL) {
            *predicate = !*predicate;
        }
        return true;
    }
    if (minic_type_is_signed_integer(common_type)) {
        int64_t left_signed;
        int64_t right_signed;
        if (!core_const_signed_value(context, left, &left_signed) ||
            !core_const_signed_value(context, right, &right_signed)) {
            return false;
        }
        switch (operator_kind) {
        case MINIC_BINARY_LESS: *predicate = left_signed < right_signed; return true;
        case MINIC_BINARY_LESS_EQUAL: *predicate = left_signed <= right_signed; return true;
        case MINIC_BINARY_GREATER: *predicate = left_signed > right_signed; return true;
        case MINIC_BINARY_GREATER_EQUAL: *predicate = left_signed >= right_signed; return true;
        default: return false;
        }
    }
    switch (operator_kind) {
    case MINIC_BINARY_LESS: *predicate = left->bits < right->bits; return true;
    case MINIC_BINARY_LESS_EQUAL: *predicate = left->bits <= right->bits; return true;
    case MINIC_BINARY_GREATER: *predicate = left->bits > right->bits; return true;
    case MINIC_BINARY_GREATER_EQUAL: *predicate = left->bits >= right->bits; return true;
    default: return false;
    }
}


static bool core_cfg_normalize_integer(const MinicCoreLowerContext *context,
                                       MinicType type,
                                       uint64_t bits,
                                       uint64_t *normalized) {
    unsigned int width;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || normalized == NULL ||
        !minic_target_info_integer_width(
            context->target, context->body->program, type, &width) ||
        width == 0U || width > 64U) {
        return false;
    }
    if (minic_type_is_bool_integer(type)) {
        *normalized = bits == 0U ? 0U : 1U;
    } else if (width == 64U) {
        *normalized = bits;
    } else {
        *normalized = bits & ((UINT64_C(1) << width) - UINT64_C(1));
    }
    return true;
}


/* M181_RESIDUAL_CONSTANT_CLOSURE: constant integer-origin pointer bits. */
static bool core_cfg_const_pointer_bits(const MinicCoreLowerContext *context,
                                        MinicExpressionId expression_id,
                                        unsigned int depth,
                                        uint64_t *bits) {
    const MinicExpression *expression;
    size_t pointer_size;
    unsigned int pointer_width;
    uint64_t mask;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || bits == NULL || depth > 64U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_pointer(expression->type) ||
        !minic_target_info_sizeof_type(context->target,
                                       context->body->program,
                                       expression->type,
                                       &pointer_size) ||
        pointer_size == 0U || pointer_size > sizeof(uint64_t)) {
        return false;
    }
    pointer_width = (unsigned int)(pointer_size * 8U);
    if (pointer_width == 0U || pointer_width > 64U) {
        return false;
    }
    mask = pointer_width == 64U ? UINT64_MAX
                                : ((UINT64_C(1) << pointer_width) - UINT64_C(1));

    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST) {
        const MinicExpression *operand;

        operand = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (operand == NULL) {
            return false;
        }
        if (minic_type_is_pointer(operand->type)) {
            return core_cfg_const_pointer_bits(
                context, expression->value.unary.operand, depth + 1U, bits);
        }
        if (minic_type_is_integer(operand->type)) {
            MinicConstValue source;
            MinicConstValue widened;
            size_t ulong_size;

            if (!minic_const_eval_integer(context->body->program,
                                          context->target,
                                          expression->value.unary.operand,
                                          &source) ||
                !minic_target_info_sizeof_type(context->target,
                                               context->body->program,
                                               minic_type_unsigned_long(),
                                               &ulong_size) ||
                ulong_size != pointer_size ||
                !minic_const_value_convert_integer(context->body->program,
                                                   context->target,
                                                   &source,
                                                   minic_type_unsigned_long(),
                                                   &widened)) {
                return false;
            }
            *bits = widened.bits & mask;
            return true;
        }
        return false;
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        const MinicExpression *left;
        const MinicExpression *right;
        MinicExpressionId pointer_id;
        MinicExpressionId integer_id;
        MinicConstValue index;
        MinicConstValue index_wide;
        uint64_t base_bits;
        uint64_t byte_delta;
        size_t element_size;

        left = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left == NULL || right == NULL) {
            return false;
        }
        if (minic_type_is_pointer(left->type) && minic_type_is_integer(right->type)) {
            pointer_id = expression->value.binary.left;
            integer_id = expression->value.binary.right;
        } else if (expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
                   minic_type_is_integer(left->type) && minic_type_is_pointer(right->type)) {
            pointer_id = expression->value.binary.right;
            integer_id = expression->value.binary.left;
        } else {
            return false;
        }
        if (!core_cfg_const_pointer_bits(context, pointer_id, depth + 1U, &base_bits) ||
            !minic_const_eval_integer(context->body->program,
                                      context->target,
                                      integer_id,
                                      &index) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &index,
                                               minic_type_unsigned_long(),
                                               &index_wide) ||
            !minic_c0_pointer_arithmetic_element_size(
                context->body->program,
                minic_target_info_data_layout(context->target),
                minic_c0_program_expression(context->body->program, pointer_id)->type,
                &element_size) ||
            element_size == 0U) {
            return false;
        }
        byte_delta = index_wide.bits * (uint64_t)element_size;
        *bits = expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT
                    ? (base_bits - byte_delta) & mask
                    : (base_bits + byte_delta) & mask;
        return true;
    }
    return false;
}

/* The numeric address of an external/global object is linker-owned, but the
 * relative positions of its members and its one-past endpoint are known from
 * DataLayout.  GNU Linux uses this in BUILD_BUG_ON guards for sentinel pointers
 * such as (struct list_head *)&migrate_nodes.prev versus &migrate_nodes + 1.
 * Prove only two addresses derived from the SAME complete object; never
 * compare unrelated symbols or guess an absolute linker address. */
typedef struct MinicCoreGlobalAddressFact {
    MinicGlobalObjectId object_id;
    size_t byte_offset;
} MinicCoreGlobalAddressFact;

static bool core_cfg_global_address_fact(const MinicCoreLowerContext *context,
                                         MinicExpressionId expression_id,
                                         unsigned int depth,
                                         MinicCoreGlobalAddressFact *fact) {
    const MinicC0Program *program;
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || fact == NULL || depth > 32U) {
        return false;
    }
    program = context->body->program;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_pointer(expression->type)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        const MinicExpression *operand = minic_c0_program_expression(
            program, expression->value.unary.operand);
        size_t from_size;
        size_t to_size;
        if (operand == NULL || !minic_type_is_pointer(operand->type) ||
            !minic_target_info_sizeof_type(context->target, program,
                                           operand->type, &from_size) ||
            !minic_target_info_sizeof_type(context->target, program,
                                           expression->type, &to_size) ||
            from_size != to_size) {
            return false;
        }
        return core_cfg_global_address_fact(context, expression->value.unary.operand,
                                            depth + 1U, fact);
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        const MinicExpression *addressed = minic_c0_program_expression(
            program, expression->value.unary.operand);
        if (addressed == NULL) {
            return false;
        }
        if (addressed->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
            fact->object_id = addressed->value.global_object_id;
            fact->byte_offset = 0U;
            return minic_c0_program_global_object(program, fact->object_id) != NULL;
        }
        if (addressed->kind == MINIC_EXPRESSION_MEMBER) {
            const MinicRecord *record = minic_c0_program_record(
                program, addressed->value.member.record_id);
            const MinicRecordField *field = minic_c0_record_field(
                record, addressed->value.member.field_index);
            MinicCoreGlobalAddressFact base;
            size_t field_offset;
            if (field == NULL || field->is_bit_field ||
                !core_cfg_global_address_fact(context,
                    addressed->value.member.base, depth + 1U, &base) ||
                !minic_data_layout_record_field_offset(
                    minic_target_info_data_layout(context->target), program, record,
                    addressed->value.member.field_index, &field_offset) ||
                field_offset > SIZE_MAX - base.byte_offset) {
                return false;
            }
            fact->object_id = base.object_id;
            fact->byte_offset = base.byte_offset + field_offset;
            return true;
        }
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
        const MinicExpression *base_expression = minic_c0_program_expression(
            program, expression->value.binary.left);
        const MinicExpression *index_expression = minic_c0_program_expression(
            program, expression->value.binary.right);
        MinicCoreGlobalAddressFact base;
        MinicConstValue index;
        MinicConstValue widened;
        size_t element_size;
        size_t object_size;
        const MinicGlobalObject *object;
        if (base_expression == NULL || index_expression == NULL ||
            !minic_type_is_pointer(base_expression->type) ||
            !minic_type_is_integer(index_expression->type) ||
            !core_cfg_global_address_fact(context, expression->value.binary.left,
                                          depth + 1U, &base) ||
            !minic_const_eval_integer(program, context->target,
                                      expression->value.binary.right, &index) ||
            !minic_const_value_convert_integer(program, context->target, &index,
                                               minic_type_long(), &widened) ||
            (int64_t)widened.bits < 0 ||
            !minic_c0_pointer_arithmetic_element_size(
                program, minic_target_info_data_layout(context->target),
                base_expression->type, &element_size) || element_size == 0U ||
            widened.bits > (SIZE_MAX - base.byte_offset) / element_size) {
            return false;
        }
        object = minic_c0_program_global_object(program, base.object_id);
        if (object == NULL ||
            !minic_target_info_sizeof_type(context->target, program,
                                           object->type, &object_size) ||
            base.byte_offset + (size_t)widened.bits * element_size > object_size) {
            return false;
        }
        fact->object_id = base.object_id;
        fact->byte_offset = base.byte_offset + (size_t)widened.bits * element_size;
        return true;
    }
    return false;
}

/* Resolve an initialized member inside nested anonymous unions/records to
 * a byte offset relative to its exact compound-literal backing local. */
static bool core_cfg_compound_member_layout(
    const MinicC0Program *program,
    const MinicDataLayout *layout,
    const MinicExpression *member,
    MinicLocalId root_local,
    unsigned int depth,
    size_t *byte_offset,
    size_t *bit_offset) {
    const MinicExpression *address;
    const MinicExpression *parent;
    const MinicRecord *record;
    const MinicRecordField *field;
    size_t parent_offset = 0U;
    size_t local_offset;

    if (program == NULL || layout == NULL || member == NULL ||
        member->kind != MINIC_EXPRESSION_MEMBER ||
        byte_offset == NULL || bit_offset == NULL || depth > 8U) {
        return false;
    }
    address = minic_c0_program_expression(program, member->value.member.base);
    parent = address != NULL && address->kind == MINIC_EXPRESSION_ADDRESS_OF
                 ? minic_c0_program_expression(program, address->value.unary.operand)
                 : NULL;
    if (parent == NULL) {
        return false;
    }
    if (parent->kind == MINIC_EXPRESSION_LOCAL) {
        if (parent->value.local_id != root_local) {
            return false;
        }
    } else if (parent->kind == MINIC_EXPRESSION_MEMBER) {
        size_t parent_bit_offset;
        if (!core_cfg_compound_member_layout(program, layout, parent, root_local,
                                             depth + 1U, &parent_offset,
                                             &parent_bit_offset) ||
            parent_bit_offset != 0U) {
            return false;
        }
    } else {
        return false;
    }
    record = minic_c0_program_record(program, member->value.member.record_id);
    field = minic_c0_record_field(record, member->value.member.field_index);
    if (field == NULL ||
        !minic_data_layout_record_field_layout(
            layout, program, record, member->value.member.field_index,
            &local_offset, bit_offset) ||
        local_offset > SIZE_MAX - parent_offset) {
        return false;
    }
    *byte_offset = parent_offset + local_offset;
    return true;
}

/* GNU-style aggregate layout tests inspect bytes of a constant record
 * compound literal.  Build a bounded, bit-accurate image from its frontend
 * initializer assignments: unknown padding is never treated as constant.
 * Only the target's explicit little-endian DataLayout and assignments to
 * members of this exact compound-literal local are admitted. */
static bool core_cfg_const_record_compound_read(
    const MinicCoreLowerContext *context,
    const MinicExpression *compound,
    const MinicExpression *subscript,
    const MinicConstValue *index,
    MinicConstValue *result) {
    enum { MAX_CONST_RECORD_BYTES = 256 };
    unsigned char bytes[MAX_CONST_RECORD_BYTES] = {0};
    unsigned char known[MAX_CONST_RECORD_BYTES] = {0};
    const MinicC0Program *program;
    const MinicDataLayout *layout;
    const MinicBlock *initializer;
    const MinicRecord *record;
    size_t record_size;
    size_t element_size;
    size_t read_offset;
    size_t i;
    uint64_t read_bits = 0U;

    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || context->target == NULL ||
        compound == NULL || subscript == NULL || index == NULL || result == NULL ||
        compound->kind != MINIC_EXPRESSION_COMPOUND_LITERAL ||
        !minic_type_is_record(compound->type) ||
        !minic_type_is_integer(subscript->type) ||
        minic_type_is_volatile(subscript->type)) {
        return false;
    }
    program = context->body->program;
    layout = minic_target_info_data_layout(context->target);
    if (layout == NULL || layout->byte_order != MINIC_BYTE_ORDER_LITTLE ||
        !minic_target_info_sizeof_type(context->target, program, compound->type,
                                       &record_size) ||
        record_size == 0U || record_size > MAX_CONST_RECORD_BYTES ||
        !minic_target_info_sizeof_type(context->target, program, subscript->type,
                                       &element_size) ||
        element_size == 0U || element_size > sizeof(read_bits) ||
        element_size > record_size ||
        index->bits > (record_size - element_size) / element_size) {
        return false;
    }
    read_offset = (size_t)index->bits * element_size;
    record = minic_c0_program_record(program, compound->type.record_id);
    initializer = minic_c0_program_block(
        program, compound->value.compound_literal.initializer_block);
    if (record == NULL || initializer == NULL ||
        initializer->statement_count > 2U * MAX_CONST_RECORD_BYTES) {
        return false;
    }

    for (i = 0U; i < initializer->statement_count; ++i) {
        const MinicStatement *statement = minic_c0_program_statement(
            program, initializer->statements[i]);
        const MinicExpression *target;
        const MinicRecordField *field;
        const MinicRecord *field_record;
        MinicConstValue constant;
        MinicConstValue converted;
        size_t byte_offset;
        size_t bit_offset;
        size_t width;
        size_t bit;

        if (statement == NULL || statement->kind != MINIC_STATEMENT_ASSIGN ||
            statement->target_expression == MINIC_EXPRESSION_INVALID ||
            statement->expression == MINIC_EXPRESSION_INVALID) {
            return false;
        }
        target = minic_c0_program_expression(program, statement->target_expression);
        if (target == NULL || target->kind != MINIC_EXPRESSION_MEMBER) {
            return false;
        }
        field_record = minic_c0_program_record(program, target->value.member.record_id);
        field = minic_c0_record_field(field_record, target->value.member.field_index);
        if (field == NULL || !minic_type_is_integer(target->type) ||
            !core_const_eval_integer_with_locals(context, statement->expression,
                                                 &constant) ||
            !minic_const_value_convert_integer(program, context->target,
                                               &constant, target->type, &converted) ||
            !core_cfg_compound_member_layout(
                program, layout, target,
                compound->value.compound_literal.local_id, 0U,
                &byte_offset, &bit_offset) || byte_offset >= record_size) {
            return false;
        }
        if (field->is_bit_field) {
            width = field->bit_width;
            if (width == 0U || width > 64U ||
                bit_offset > record_size * 8U - byte_offset * 8U ||
                width > record_size * 8U - byte_offset * 8U - bit_offset) {
                return false;
            }
            for (bit = 0U; bit < width; ++bit) {
                size_t position = bit_offset + bit;
                size_t offset = byte_offset + position / 8U;
                unsigned char mask = (unsigned char)(1U << (position % 8U));
                known[offset] |= mask;
                if (((converted.bits >> bit) & UINT64_C(1)) != 0U) {
                    bytes[offset] |= mask;
                } else {
                    bytes[offset] &= (unsigned char)~mask;
                }
            }
        } else {
            if (!minic_target_info_sizeof_type(context->target, program,
                                               target->type, &width) ||
                width == 0U || width > 8U ||
                width > record_size - byte_offset) {
                return false;
            }
            for (bit = 0U; bit < width; ++bit) {
                known[byte_offset + bit] = 0xffU;
                bytes[byte_offset + bit] =
                    (unsigned char)(converted.bits >> (8U * bit));
            }
        }
    }
    for (i = 0U; i < element_size; ++i) {
        if (known[read_offset + i] != 0xffU) {
            return false;
        }
        read_bits |= (uint64_t)bytes[read_offset + i] << (8U * i);
    }
    result->type = subscript->type;
    result->bits = read_bits;
    return true;
}

/* A GNU Linux BUILD_BUG_ON can read a zero-initialized union through a
 * different member: `union { u32 word; arch_spinlock_t lock; } u =
 * { .lock = __ARCH_SPIN_LOCK_UNLOCKED }; ... u.word`.  The frontend expands
 * that automatic initializer into explicit source `is_initializer_write`
 * stores; do not mistake a mere declaration for a proven constant. */
static bool core_cfg_union_root_local(const MinicC0Program *p,
                                       MinicExpressionId id, unsigned int depth,
                                       MinicLocalId *root) {
    const MinicExpression *e;
    if (p == NULL || root == NULL || depth > 12U ||
        (e = minic_c0_program_expression(p, id)) == NULL) return false;
    if (e->kind == MINIC_EXPRESSION_LOCAL) {
        *root = e->value.local_id;
        return true;
    }
    if (e->kind == MINIC_EXPRESSION_ADDRESS_OF ||
        e->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return core_cfg_union_root_local(p, e->value.unary.operand,
                                         depth + 1U, root);
    }
    if (e->kind == MINIC_EXPRESSION_MEMBER) {
        return core_cfg_union_root_local(p, e->value.member.base,
                                         depth + 1U, root);
    }
    return false;
}

typedef struct CoreUnionRefCheck {
    MinicExpressionId needle;
    bool used;
} CoreUnionRefCheck;

static bool core_cfg_union_check_child(MinicExpressionId *id, void *opaque) {
    CoreUnionRefCheck *check = (CoreUnionRefCheck *)opaque;
    if (id != NULL && *id == check->needle) check->used = true;
    return true;
}

/* Exact AST edge ownership: a local union may only be addressed to select
 * members, and each address of that local/member must itself only be used
 * as a member base.  This rejects address escape through calls/casts/asm,
 * including aliases that could modify the union behind our zero proof. */
static bool core_cfg_union_id_is_only_member_address(
    const MinicC0Program *p, MinicExpressionId id,
    MinicExpressionKind allowed_parent) {
    size_t i;
    for (i = 0U; i < p->expression_count; ++i) {
        const MinicExpression *parent = &p->expressions[i];
        MinicExpression copy = *parent;
        CoreUnionRefCheck check = {id, false};
        if (!minic_c0_expression_visit_child_id_refs(
                &copy, core_cfg_union_check_child, &check)) return false;
        if (check.used &&
            (parent->kind != allowed_parent ||
             (allowed_parent == MINIC_EXPRESSION_ADDRESS_OF &&
              parent->value.unary.operand != id) ||
             (allowed_parent == MINIC_EXPRESSION_MEMBER &&
              parent->value.member.base != id))) return false;
    }
    for (i = 0U; i < p->statement_count; ++i) {
        if (p->statements[i].expression == id ||
            p->statements[i].target_expression == id) return false;
    }
    for (i = 0U; i < p->inline_asm_count; ++i) {
        const MinicInlineAsm *a = &p->inline_asms[i];
        size_t j;
        for (j = 0U; j < a->input_count; ++j)
            if (a->inputs[j].expression == id) return false;
        for (j = 0U; j < a->output_count; ++j)
            if (a->outputs[j].expression == id) return false;
    }
    return true;
}

static bool core_cfg_zero_union_scalar_read(
    const MinicCoreLowerContext *c, MinicExpressionId id,
    MinicConstValue *result) {
    const MinicC0Program *p;
    const MinicDataLayout *layout;
    const MinicExpression *read;
    const MinicLocal *local;
    const MinicRecord *record;
    const MinicBlock *entry;
    MinicLocalId root;
    size_t union_size, read_size, read_offset, bit_offset, i;
    bool fully_initialized = false;
    size_t init_begin = SIZE_MAX;
    size_t init_count = 0U;

    if (c == NULL || c->body == NULL || c->body->program == NULL ||
        c->source_function == NULL || c->target == NULL || result == NULL) return false;
    p = c->body->program;
    read = minic_c0_program_expression(p, id);
    if (read != NULL && read->kind == MINIC_EXPRESSION_LVALUE_READ)
        read = minic_c0_program_expression(p, read->value.unary.operand);
    if (read == NULL || read->kind != MINIC_EXPRESSION_MEMBER ||
        !minic_type_is_integer(read->type) || minic_type_is_volatile(read->type) ||
        !core_cfg_union_root_local(p, read->value.member.base, 0U, &root) ||
        root < c->source_function->local_begin ||
        root - c->source_function->local_begin >= c->source_function->local_count) return false;
    local = minic_c0_program_local(p, root);
    record = local != NULL && minic_type_is_record(local->type)
                 ? minic_c0_program_record(p, local->type.record_id) : NULL;
    layout = minic_target_info_data_layout(c->target);
    if (local == NULL || record == NULL || !record->is_union ||
        !record->is_complete || local->is_array ||
        minic_type_is_volatile(local->type) ||
        !local->has_declaration_block ||
        local->declaration_block != c->source_function->body_block ||
        layout == NULL || layout->byte_order != MINIC_BYTE_ORDER_LITTLE ||
        !minic_target_info_sizeof_type(c->target, p, local->type, &union_size) ||
        !minic_target_info_sizeof_type(c->target, p, read->type, &read_size) ||
        union_size == 0U || union_size > 8U || read_size != union_size ||
        !core_cfg_compound_member_layout(
            p, layout, read, root, 0U, &read_offset, &bit_offset) ||
        read_offset != 0U || bit_offset != 0U ||
        (entry = minic_c0_program_block(p, local->declaration_block)) == NULL ||
        entry->statement_count == 0U || entry->statement_count > 64U) return false;

    /* A contiguous group of local declaration stores in the entry block
     * must initialize every byte via an all-zero scalar.  Ordinary earlier
     * statements are allowed, but an external CFG reentry label/goto is not:
     * it could reach the later read while bypassing this initialization. */
    for (i = 0U; i < entry->statement_count; ++i) {
        const MinicStatement *s = minic_c0_program_statement(p, entry->statements[i]);
        if (s == NULL || s->kind == MINIC_STATEMENT_LABEL ||
            s->kind == MINIC_STATEMENT_GOTO ||
            s->kind == MINIC_STATEMENT_CASE ||
            s->kind == MINIC_STATEMENT_DEFAULT ||
            s->kind == MINIC_STATEMENT_SWITCH) return false;
    }
    for (i = 0U; i < entry->statement_count; ++i) {
        const MinicStatement *s = minic_c0_program_statement(p, entry->statements[i]);
        const MinicExpression *target;
        MinicLocalId target_root;
        size_t offset, target_bit, target_size;
        bool is_root;
        is_root = core_cfg_union_root_local(
            p, s->target_expression, 0U, &target_root) && target_root == root;
        if (!is_root) {
            if (init_count != 0U) break;
            continue;
        }
        if (init_begin == SIZE_MAX) init_begin = i;
        if (s->kind != MINIC_STATEMENT_ASSIGN || !s->is_initializer_write)
            return false;
        target = minic_c0_program_expression(p, s->target_expression);
        if (target == NULL || target->kind != MINIC_EXPRESSION_MEMBER ||
            !minic_type_is_integer(target->type) ||
            !core_program_expr_is_literal_zero(c, s->expression) ||
            !core_cfg_compound_member_layout(
                p, layout, target, root, 0U, &offset, &target_bit) ||
            target_bit != 0U ||
            !minic_target_info_sizeof_type(
                c->target, p, target->type, &target_size) ||
            offset > union_size || target_size > union_size - offset)
            return false;
        if (offset == 0U && target_size == union_size) fully_initialized = true;
        ++init_count;
    }
    if (!fully_initialized || init_begin == SIZE_MAX || init_count == 0U)
        return false;
    for (i = 0U; i < p->statement_count; ++i) {
        const MinicStatement *s = &p->statements[i];
        MinicLocalId target_root;
        size_t j;
        bool in_prefix = false;
        if (!core_cfg_union_root_local(
                p, s->target_expression, 0U, &target_root) || target_root != root)
            continue;
        for (j = 0U; j < init_count; ++j)
            if (entry->statements[init_begin + j] == (MinicStatementId)i)
                in_prefix = true;
        if (!in_prefix) return false;
    }
    for (i = 0U; i < p->inline_asm_count; ++i) {
        const MinicInlineAsm *a = &p->inline_asms[i];
        size_t j;
        for (j = 0U; j < a->output_count; ++j) {
            MinicLocalId output_root;
            if (core_cfg_union_root_local(
                    p, a->outputs[j].expression, 0U, &output_root) &&
                output_root == root) return false;
        }
    }
    for (i = 0U; i < p->expression_count; ++i) {
        const MinicExpression *e = &p->expressions[i];
        MinicLocalId expr_root;
        if (e->kind == MINIC_EXPRESSION_LOCAL && e->value.local_id == root &&
            !core_cfg_union_id_is_only_member_address(
                p, (MinicExpressionId)i, MINIC_EXPRESSION_ADDRESS_OF)) return false;
        if (e->kind == MINIC_EXPRESSION_ADDRESS_OF &&
            core_cfg_union_root_local(p, e->value.unary.operand, 0U, &expr_root) &&
            expr_root == root &&
            !core_cfg_union_id_is_only_member_address(
                p, (MinicExpressionId)i, MINIC_EXPRESSION_MEMBER)) return false;
        if ((e->kind == MINIC_EXPRESSION_ASSIGNMENT ||
             e->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) &&
            core_cfg_union_root_local(p, e->value.binary.left, 0U, &expr_root) &&
            expr_root == root) return false;
        if (e->kind == MINIC_EXPRESSION_UNARY &&
            e->value.unary.operator_kind > MINIC_UNARY_BITWISE_NOT &&
            core_cfg_union_root_local(p, e->value.unary.operand, 0U, &expr_root) &&
            expr_root == root) return false;
    }
    result->type = read->type;
    result->bits = 0U;
    return true;
}

/* A deliberately one-sided bit analysis for Linux's BUILD_BUG_ON masks.
 * It tracks which bits MAY be one, never which bits must be one.  A conditional
 * whose alternatives are 0 and BIT(n), for example, can carry only BIT(n)
 * even when the condition is runtime-dependent.  The caller separately
 * checks that evaluating the complete expression has no side effects before
 * replacing the expression with a constant.  Unsupported operators widen to
 * the full type mask instead of assuming a value. */
static uint64_t core_cfg_integer_width_mask(unsigned int width) {
    return width >= 64U ? UINT64_MAX : (UINT64_C(1) << width) - UINT64_C(1);
}

static uint64_t core_cfg_possible_one_bits(
    const MinicCoreLowerContext *context, MinicExpressionId id,
    unsigned int depth);

/* Interpret an operand's possible bits in the containing integer type.
 * Widening signed types may extend the sign bit into all upper bits; do not
 * accidentally treat those upper bits as impossible. */
static uint64_t core_cfg_possible_one_bits_as(
    const MinicCoreLowerContext *context, MinicExpressionId id,
    unsigned int result_width, unsigned int depth) {
    const MinicExpression *operand = minic_c0_program_expression(
        context->body->program, id);
    unsigned int operand_width;
    uint64_t bits;
    if (operand == NULL || !minic_type_is_integer(operand->type) ||
        !minic_target_info_integer_width(context->target,
                                         context->body->program,
                                         operand->type, &operand_width) ||
        operand_width == 0U || operand_width > 64U) {
        return UINT64_MAX;
    }
    bits = core_cfg_possible_one_bits(context, id, depth);
    if (result_width > operand_width &&
        minic_type_is_signed_integer(operand->type) &&
        (bits & (UINT64_C(1) << (operand_width - 1U))) != 0U) {
        bits |= ~core_cfg_integer_width_mask(operand_width);
    }
    return bits & core_cfg_integer_width_mask(result_width);
}

static uint64_t core_cfg_possible_one_bits(
    const MinicCoreLowerContext *context, MinicExpressionId id,
    unsigned int depth) {
    const MinicExpression *expression;
    MinicConstValue constant;
    unsigned int width;
    uint64_t full;

    if (depth > 48U || context == NULL || context->body == NULL ||
        context->body->program == NULL || context->target == NULL) {
        return UINT64_MAX;
    }
    expression = minic_c0_program_expression(context->body->program, id);
    if (expression == NULL || !minic_type_is_integer(expression->type) ||
        !minic_target_info_integer_width(context->target,
                                         context->body->program,
                                         expression->type, &width) ||
        width == 0U || width > 64U) {
        return UINT64_MAX;
    }
    full = core_cfg_integer_width_mask(width);
    if (minic_const_eval_integer(context->body->program, context->target,
                                 id, &constant)) {
        MinicConstValue converted;
        if (minic_const_value_convert_integer(context->body->program,
                                              context->target, &constant,
                                              expression->type, &converted)) {
            return converted.bits & full;
        }
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_LOCAL:
    case MINIC_EXPRESSION_LVALUE_READ: {
        MinicLocalId fact_local;
        uint64_t possible_bits;
        if (minic_core_lower_direct_local_read(context, id, &fact_local) &&
            minic_core_local_fact_possible_one_bits(
                context, fact_local, &possible_bits)) {
            return possible_bits & full;
        }
        break;
    }
    case MINIC_EXPRESSION_BINARY:
        if (expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {
            return (core_cfg_possible_one_bits_as(
                        context, expression->value.binary.left, width, depth + 1U) &
                    core_cfg_possible_one_bits_as(
                        context, expression->value.binary.right, width, depth + 1U)) & full;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR ||
            expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_XOR) {
            return (core_cfg_possible_one_bits_as(
                        context, expression->value.binary.left, width, depth + 1U) |
                    core_cfg_possible_one_bits_as(
                        context, expression->value.binary.right, width, depth + 1U)) & full;
        }
        break;
    case MINIC_EXPRESSION_CONDITIONAL:
        return (core_cfg_possible_one_bits_as(
                    context, expression->value.conditional.when_true, width, depth + 1U) |
                core_cfg_possible_one_bits_as(
                    context, expression->value.conditional.when_false, width, depth + 1U)) & full;
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION: {
        return core_cfg_possible_one_bits_as(
            context, expression->value.unary.operand, width, depth + 1U);
    }
    default:
        break;
    }
    return full;
}

/* Nonnegative value intervals are distinct from may-one bit masks: the
 * numerical maximum of 52 and 46 is 52, even though (52|46) is 62.  GNU
 * Linux uses this in BUILD_BUG_ON((runtime ? 52 : 46)-12 > 58) and in
 * the two known x86 virtual-base alternatives.  Never evaluate or erase the
 * runtime selection condition here; this only proves result bounds. */
static bool core_cfg_nonnegative_range_impl(
    const MinicCoreLowerContext *context, MinicExpressionId id,
    unsigned int depth, uint64_t *minimum, uint64_t *maximum) {
    const MinicExpression *expression;
    MinicConstValue constant, converted;
    unsigned int width;
    uint64_t type_max;
    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || context->target == NULL ||
        minimum == NULL || maximum == NULL || depth > 32U) return false;
    expression = minic_c0_program_expression(context->body->program, id);
    if (expression == NULL || !minic_type_is_integer(expression->type) ||
        minic_type_is_bool_integer(expression->type) ||
        !minic_target_info_integer_width(context->target,
            context->body->program, expression->type, &width) ||
        width == 0U || width > 64U) return false;
    type_max = minic_type_is_signed_integer(expression->type)
        ? ((UINT64_C(1) << (width - 1U)) - UINT64_C(1))
        : core_cfg_integer_width_mask(width);
    if (core_const_eval_integer_with_locals(context, id, &constant) &&
        minic_const_value_convert_integer(context->body->program,
            context->target, &constant, expression->type, &converted) &&
        converted.bits <= type_max) {
        *minimum = *maximum = converted.bits;
        return true;
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_LOCAL:
    case MINIC_EXPRESSION_LVALUE_READ: {
        MinicLocalId local;
        return minic_core_lower_direct_local_read(context, id, &local) &&
            minic_core_local_fact_nonnegative_range_get(
                context, local, minimum, maximum) && *maximum <= type_max;
    }
    case MINIC_EXPRESSION_CONDITIONAL: {
        uint64_t true_min, true_max, false_min, false_max;
        if (!core_cfg_nonnegative_range_impl(context,
                expression->value.conditional.when_true, depth + 1U,
                &true_min, &true_max) ||
            !core_cfg_nonnegative_range_impl(context,
                expression->value.conditional.when_false, depth + 1U,
                &false_min, &false_max)) return false;
        *minimum = true_min < false_min ? true_min : false_min;
        *maximum = true_max > false_max ? true_max : false_max;
        return *maximum <= type_max;
    }
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
        return core_cfg_nonnegative_range_impl(context,
                   expression->value.unary.operand, depth + 1U,
                   minimum, maximum) && *maximum <= type_max;
    case MINIC_EXPRESSION_BINARY:
        if (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT) {
            uint64_t offset;
            MinicConstValue rhs;
            if (!core_cfg_nonnegative_range_impl(context,
                    expression->value.binary.left, depth + 1U,
                    minimum, maximum) ||
                !core_const_eval_integer_with_locals(context,
                    expression->value.binary.right, &rhs) ||
                !minic_const_value_convert_integer(context->body->program,
                    context->target, &rhs, expression->type, &converted))
                return false;
            offset = converted.bits;
            if (offset > type_max || *minimum < offset || *maximum > type_max)
                return false;
            *minimum -= offset;
            *maximum -= offset;
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

bool minic_core_cfg_nonnegative_range(
    const MinicCoreLowerContext *context, MinicExpressionId id,
    uint64_t *minimum, uint64_t *maximum) {
    return core_cfg_nonnegative_range_impl(context, id, 0U,
                                           minimum, maximum);
}

/* One-sided false-only CFG proof. The caller lowers the FULL original
 * comparison (including conditional feature probes and volatile accesses)
 * before skipping an impossible diagnostic arm. Signed negatives and any
 * arithmetic wrap are rejected by the nonnegative range admission. */
bool minic_core_cfg_bounded_comparison_is_false(
    const MinicCoreLowerContext *context, MinicExpressionId id) {
    const MinicExpression *expression;
    unsigned int negations = 0U, depth;
    uint64_t left_min, left_max, right_min, right_max;
    if (context == NULL || context->body == NULL ||
        context->body->program == NULL) return false;
    for (depth = 0U; depth < 8U; ++depth) {
        expression = minic_c0_program_expression(context->body->program, id);
        if (expression == NULL) return false;
        if (expression->kind == MINIC_EXPRESSION_UNARY &&
            expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
            negations ^= 1U;
            id = expression->value.unary.operand;
        } else break;
    }
    if (negations != 0U) return false;
    expression = minic_c0_program_expression(context->body->program, id);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_BINARY)
        return false;
    if (expression->value.binary.operator_kind != MINIC_BINARY_GREATER_EQUAL &&
        expression->value.binary.operator_kind != MINIC_BINARY_LESS) return false;
    if (!minic_core_cfg_nonnegative_range(
            context, expression->value.binary.left, &left_min, &left_max) ||
        !minic_core_cfg_nonnegative_range(
            context, expression->value.binary.right, &right_min, &right_max))
        return false;
    if (expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)
        return left_max < right_min;
    if (expression->value.binary.operator_kind == MINIC_BINARY_LESS)
        return left_min >= right_max;
    return false;
}

/* Unlike full integer constant folding, this structural proof does not
 * require a pure predicate.  A caller must still LOWER the condition and
 * retain its effects before selecting the only possible CFG successor. */
bool minic_core_cfg_disjoint_bitmask_is_zero(
    const MinicCoreLowerContext *context, MinicExpressionId id) {
    const MinicExpression *expression;
    unsigned int negations = 0U;
    unsigned int depth;

    if (context == NULL || context->body == NULL ||
        context->body->program == NULL) {
        return false;
    }
    for (depth = 0U; depth < 8U; ++depth) {
        expression = minic_c0_program_expression(context->body->program, id);
        if (expression == NULL) return false;
        if (expression->kind == MINIC_EXPRESSION_UNARY &&
            expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
            id = expression->value.unary.operand;
            negations ^= 1U;
        } else {
            break;
        }
    }
    expression = minic_c0_program_expression(context->body->program, id);
    return negations == 0U && expression != NULL &&
           expression->kind == MINIC_EXPRESSION_BINARY &&
           expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND &&
           core_cfg_possible_one_bits(context, id, 0U) == 0U;
}

bool minic_core_cfg_pure_guard_truth(void *opaque,
                                     MinicExpressionId condition_id,
                                     bool *truth) {
    const MinicCoreLowerContext *context = (const MinicCoreLowerContext *)opaque;
    MinicConstValue constant;
    bool zero;
    if (context == NULL || truth == NULL ||
        !minic_core_cfg_pure_call_argument(context, condition_id, 0U) ||
        !core_const_eval_integer_with_locals(context, condition_id, &constant) ||
        !minic_const_value_is_zero(context->body->program, context->target,
                                   &constant, &zero)) return false;
    *truth = !zero;
    return true;
}

bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,
                                                MinicExpressionId expression_id,
                                                MinicConstValue *value) {
    const MinicExpression *expression;
    MinicConstValue operand_value;
    MinicLocalId local_id;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || value == NULL) {
        return false;
    }
    if (minic_const_eval_integer(
            context->body->program, context->target, expression_id, value)) {
        return true;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (core_cfg_zero_union_scalar_read(context, expression_id, value)) {
        return true;
    }
    if (minic_core_const_global_record_scalar(context, expression_id, value)) {
        return true;
    }
    {
        const MinicExpression *read = expression;
        const MinicExpression *subscript;
        const MinicExpression *base;
        const MinicGlobalObject *object;
        MinicConstValue index_value;
        MinicConstValue element_value;
        MinicExpressionId base_id;
        unsigned int unwrap_depth;
        bool compound_provenance = true;

        if (read->kind == MINIC_EXPRESSION_LVALUE_READ) {
            read = minic_c0_program_expression(
                context->body->program, read->value.unary.operand);
        }
        if (read != NULL && read->kind == MINIC_EXPRESSION_SUBSCRIPT) {
            subscript = read;
            base_id = subscript->value.subscript.base;
            base = minic_c0_program_expression(context->body->program, base_id);
            for (unwrap_depth = 0U; base != NULL && unwrap_depth < 8U; ++unwrap_depth) {
                if (base->kind == MINIC_EXPRESSION_CAST ||
                    base->kind == MINIC_EXPRESSION_BITCAST ||
                    base->kind == MINIC_EXPRESSION_CONVERSION) {
                    const MinicExpression *operand = minic_c0_program_expression(
                        context->body->program, base->value.unary.operand);
                    if (operand == NULL || !minic_type_is_pointer(base->type) ||
                        !minic_type_is_pointer(operand->type)) {
                        compound_provenance = false;
                    }
                    base_id = base->value.unary.operand;
                    base = operand;
                    continue;
                }
                if (base->kind == MINIC_EXPRESSION_LVALUE_READ ||
                    base->kind == MINIC_EXPRESSION_ADDRESS_OF) {
                    if (base->kind == MINIC_EXPRESSION_LVALUE_READ) {
                        compound_provenance = false;
                    }
                    base_id = base->value.unary.operand;
                    base = minic_c0_program_expression(context->body->program, base_id);
                    continue;
                }
                break;
            }
            if (compound_provenance && base != NULL &&
                base->kind == MINIC_EXPRESSION_COMPOUND_LITERAL &&
                core_const_eval_integer_with_locals(
                    context, subscript->value.subscript.index, &index_value) &&
                core_cfg_const_record_compound_read(
                    context, base, subscript, &index_value, &element_value) &&
                minic_const_value_convert_integer(context->body->program,
                                                  context->target, &element_value,
                                                  expression->type, value)) {
                return true;
            }
            if (base != NULL && base->kind == MINIC_EXPRESSION_GLOBAL_OBJECT &&
                core_const_eval_integer_with_locals(
                    context, subscript->value.subscript.index, &index_value)) {
                object = minic_c0_program_global_object(
                    context->body->program, base->value.global_object_id);
                if (object != NULL && object->is_internal && object->is_read_only &&
                    !object->is_extern && object->relocation_count == 0U &&
                    index_value.bits < object->initializer_count) {
                    element_value.type = subscript->type;
                    element_value.bits = object->initializer_values[index_value.bits];
                    if (minic_const_value_convert_integer(context->body->program,
                                                          context->target,
                                                          &element_value,
                                                          expression->type,
                                                          value)) {
                        return true;
                    }
                }
            }
        }
    }
    {
        const MinicExpression *object_expression = expression;
        if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
            const MinicExpression *operand = minic_c0_program_expression(
                context->body->program, expression->value.unary.operand);
            if (operand != NULL) {
                object_expression = operand;
            }
        }
        if (object_expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
            const MinicGlobalObject *object = minic_c0_program_global_object(
                context->body->program, object_expression->value.global_object_id);
            MinicType object_type;
            if (object != NULL && object->is_internal && object->is_read_only &&
                !object->is_extern && object->relocation_count == 0U &&
                minic_type_unqualified(object->type, &object_type) &&
                minic_type_is_integer(object_type) &&
                (object->initializer_count == 1U || object->is_zero_initialized)) {
                MinicConstValue source;
                source.type = object_type;
                source.bits = object->initializer_count == 1U
                                  ? object->initializer_values[0]
                                  : 0U;
                if (minic_const_value_convert_integer(context->body->program,
                                                      context->target,
                                                      &source,
                                                      expression->type,
                                                      value)) {
                    return true;
                }
            }
        }
    }
    /* M184_STATEMENT_EXPRESSION_CONSTANT_CFG: interpret only the straight-line
       local-constant subset of GNU ({ ... }) expressions. This is intentionally
       not a general AST interpreter. */
    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
        MinicConstValue guarded_value;
        const MinicBlock *statement_block;
        MinicCoreLowerContext nested_context;
        MinicCoreLocalIntegerConstant *nested_facts = NULL;
        size_t local_count;
        size_t statement_index;
        bool success = false;

        if (context->source_function == NULL ||
            expression->value.statement_expression.block == MINIC_BLOCK_INVALID ||
            expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return false;
        }
        /* Reuse Compiler's effect-free GNU do/while BUILD_BUG guard proof.
         * The shared CFG checker rejects unproven loops, side-effecting
         * prefixes, and any synthetic label with external reentry. */
        if (minic_opt_pure_statement_block(
                context->body->program, context->target, context->reentry_index,
                expression->value.statement_expression.block, (void *)context,
                minic_core_cfg_pure_guard_truth, 0U) &&
            minic_core_cfg_pure_call_argument(
                context, expression->value.statement_expression.result, 0U) &&
            core_const_eval_integer_with_locals(
                context, expression->value.statement_expression.result,
                &guarded_value) &&
            minic_const_value_convert_integer(context->body->program,
                                              context->target, &guarded_value,
                                              expression->type, value)) {
            return true;
        }
        statement_block = minic_c0_program_block(
            context->body->program, expression->value.statement_expression.block);
        if (statement_block == NULL || statement_block->statement_count == 0U) {
            return false;
        }
        local_count = context->source_function->local_count;
        if (local_count != 0U) {
            nested_facts = (MinicCoreLocalIntegerConstant *)calloc(
                local_count, sizeof(*nested_facts));
            if (nested_facts == NULL) {
                return false;
            }
            if (context->local_integer_constants != NULL) {
                (void)memcpy(nested_facts,
                             context->local_integer_constants,
                             local_count * sizeof(*nested_facts));
            }
        }
        nested_context = *context;
        nested_context.local_integer_constants = nested_facts;

        for (statement_index = 0U;
             statement_index < statement_block->statement_count;
             ++statement_index) {
            const MinicStatement *nested_statement = minic_c0_program_statement(
                context->body->program, statement_block->statements[statement_index]);

            if (nested_statement == NULL ||
                nested_statement->cleanup_context != nested_statement->cleanup_stop_context) {
                goto statement_expression_done;
            }
            if (nested_statement->kind == MINIC_STATEMENT_ASSIGN) {
                const MinicExpression *target;
                MinicConstValue assigned;
                MinicConstValue check;

                if (nested_statement->target_expression == MINIC_EXPRESSION_INVALID ||
                    nested_statement->expression == MINIC_EXPRESSION_INVALID) {
                    goto statement_expression_done;
                }
                target = minic_c0_program_expression(
                    context->body->program, nested_statement->target_expression);
                if (target == NULL || target->kind != MINIC_EXPRESSION_LOCAL ||
                    target->value_category != MINIC_VALUE_LVALUE ||
                    !minic_type_is_integer(target->type) ||
                    !core_const_eval_integer_with_locals(
                        &nested_context, nested_statement->expression, &assigned)) {
                    goto statement_expression_done;
                }
                core_local_constant_set(
                    &nested_context, target->value.local_id, &assigned);
                if (!minic_core_local_fact_get(
                        &nested_context, target->value.local_id, &check)) {
                    goto statement_expression_done;
                }
                continue;
            }
            if (nested_statement->kind == MINIC_STATEMENT_EXPRESSION &&
                statement_index + 1U == statement_block->statement_count &&
                nested_statement->expression ==
                    expression->value.statement_expression.result) {
                continue;
            }
            goto statement_expression_done;
        }
        success = core_const_eval_integer_with_locals(
            &nested_context, expression->value.statement_expression.result, value);
statement_expression_done:
        free(nested_facts);
        return success;
    }

    if (minic_core_lower_direct_local_read(context, expression_id, &local_id) &&
        minic_core_local_fact_get(context, local_id, &operand_value)) {
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &operand_value,
                                                 expression->type,
                                                 value);
    }
    /* M180_NULL_POINTER_CONDITION_FACTS: local null-pointer tracking already
       records CONFIG-off stubs such as `p = helper_returning_NULL()`.  Consume
       that fact when C converts `!p` to int so a following terminating guard is
       folded before dead external calls are emitted. */
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        const MinicExpression *operand = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (operand != NULL && minic_type_is_pointer(operand->type) &&
            minic_core_lower_known_null_pointer(
                context, expression->value.unary.operand)) {
            value->type = expression->type;
            value->bits = UINT64_C(1);
            return true;
        }
    }
    if (expression->kind == MINIC_EXPRESSION_CALL &&
        minic_core_cfg_constant_inline_call(context, expression, value)) {
        return true;
    }
    {
        const MinicExpression *compound = expression;
        if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
            const MinicExpression *operand = minic_c0_program_expression(
                context->body->program, expression->value.unary.operand);
            if (operand != NULL && operand->kind == MINIC_EXPRESSION_COMPOUND_LITERAL) {
                compound = operand;
            }
        }
        if (compound->kind == MINIC_EXPRESSION_COMPOUND_LITERAL &&
            minic_type_is_integer(compound->type)) {
            const MinicBlock *initializer = minic_c0_program_block(
                context->body->program,
                compound->value.compound_literal.initializer_block);
            if (initializer != NULL && initializer->statement_count == 1U) {
                const MinicStatement *statement = minic_c0_program_statement(
                    context->body->program, initializer->statements[0]);
                const MinicExpression *target =
                    statement != NULL && statement->target_expression != MINIC_EXPRESSION_INVALID
                        ? minic_c0_program_expression(
                              context->body->program, statement->target_expression)
                        : NULL;
                MinicConstValue initializer_value;
                if (statement != NULL && statement->kind == MINIC_STATEMENT_ASSIGN &&
                    target != NULL && target->kind == MINIC_EXPRESSION_LOCAL &&
                    target->value.local_id == compound->value.compound_literal.local_id &&
                    statement->expression != MINIC_EXPRESSION_INVALID &&
                    core_const_eval_integer_with_locals(
                        context, statement->expression, &initializer_value)) {
                    return minic_const_value_convert_integer(context->body->program,
                                                             context->target,
                                                             &initializer_value,
                                                             expression->type,
                                                             value);
                }
            }
        }
    }

    /* Integer casts of a constant integer-origin pointer preserve target bits.
       This covers BUILD_BUG_ON((unsigned long)((void *)C + K) & mask). */
    if ((expression->kind == MINIC_EXPRESSION_CAST ||
         expression->kind == MINIC_EXPRESSION_BITCAST ||
         expression->kind == MINIC_EXPRESSION_CONVERSION) &&
        minic_type_is_integer(expression->type)) {
        const MinicExpression *operand = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        uint64_t pointer_bits;
        if (operand != NULL && minic_type_is_pointer(operand->type) &&
            core_cfg_const_pointer_bits(
                context, expression->value.unary.operand, 0U, &pointer_bits)) {
            MinicConstValue pointer_integer;
            pointer_integer.type = minic_type_unsigned_long();
            pointer_integer.bits = pointer_bits;
            return minic_const_value_convert_integer(context->body->program,
                                                     context->target,
                                                     &pointer_integer,
                                                     expression->type,
                                                     value);
        }
    }

    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        if (!core_const_eval_integer_with_locals(
                context, expression->value.unary.operand, &operand_value)) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &operand_value,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        bool is_zero;
        if (!core_const_eval_integer_with_locals(
                context, expression->value.unary.operand, &operand_value) ||
            !minic_const_value_is_zero(context->body->program,
                                       context->target,
                                       &operand_value,
                                       &is_zero)) {
            return false;
        }
        value->type = expression->type;
        value->bits = is_zero ? 1U : 0U;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CONSTANT_P) {
        value->type = minic_type_int();
        value->bits = core_const_eval_integer_with_locals(
                          context, expression->value.builtin_unary.operand, &operand_value)
                          ? 1U
                          : 0U;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY) {
        const MinicExpression *operand = minic_c0_program_expression(
            context->body->program, expression->value.builtin_unary.operand);
        MinicBuiltinUnaryOperator operation =
            expression->value.builtin_unary.operator_kind;
        unsigned int width;
        uint64_t bits;
        unsigned int count = 0U;
        bool recognized =
            operation == MINIC_BUILTIN_UNARY_CLZ ||
            operation == MINIC_BUILTIN_UNARY_CLZL ||
            operation == MINIC_BUILTIN_UNARY_CLZLL ||
            operation == MINIC_BUILTIN_UNARY_CTZ ||
            operation == MINIC_BUILTIN_UNARY_CTZL ||
            operation == MINIC_BUILTIN_UNARY_CTZLL ||
            operation == MINIC_BUILTIN_UNARY_POPCOUNT ||
            operation == MINIC_BUILTIN_UNARY_POPCOUNTL ||
            operation == MINIC_BUILTIN_UNARY_POPCOUNTLL ||
            operation == MINIC_BUILTIN_UNARY_FFSLL;

        /* The frontend folds an ICE operand already. During a constant-call
         * evaluation the same intrinsic may instead read a specialized
         * callee parameter. Compute the exact target-width integer result
         * without introducing an ISA-specific KVM/fixmap exception. */
        if (recognized && operand != NULL &&
            core_const_eval_integer_with_locals(
                context, expression->value.builtin_unary.operand, &operand_value) &&
            minic_target_info_integer_width(context->target,
                                            context->body->program,
                                            operand->type, &width) &&
            width > 0U && width <= 64U) {
            bits = operand_value.bits & core_cfg_integer_width_mask(width);
            if (operation == MINIC_BUILTIN_UNARY_CLZ ||
                operation == MINIC_BUILTIN_UNARY_CLZL ||
                operation == MINIC_BUILTIN_UNARY_CLZLL) {
                if (bits == 0U) return false; /* GCC: undefined on zero */
                while ((bits & (UINT64_C(1) << (width - 1U))) == 0U) {
                    bits <<= 1U;
                    ++count;
                }
            } else if (operation == MINIC_BUILTIN_UNARY_CTZ ||
                       operation == MINIC_BUILTIN_UNARY_CTZL ||
                       operation == MINIC_BUILTIN_UNARY_CTZLL) {
                if (bits == 0U) return false;
                while ((bits & UINT64_C(1)) == 0U) {
                    bits >>= 1U;
                    ++count;
                }
            } else if (operation == MINIC_BUILTIN_UNARY_FFSLL) {
                if (bits != 0U) {
                    count = 1U;
                    while ((bits & UINT64_C(1)) == 0U) {
                        bits >>= 1U;
                        ++count;
                    }
                }
            } else {
                while (bits != 0U) {
                    bits &= bits - UINT64_C(1);
                    ++count;
                }
            }
            value->type = expression->type;
            value->bits = count;
            return true;
        }
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT &&
        expression->value.statement_expression.result != MINIC_EXPRESSION_INVALID) {
        const MinicExpression *statement_result_expression;
        MinicLocalId statement_result_local;
        MinicConstValue statement_result_local_value;
        bool statement_result_direct_local;
        bool statement_result_local_known;
        bool statement_known;

        statement_result_expression = minic_c0_program_expression(
            context->body->program, expression->value.statement_expression.result);
        statement_result_local = MINIC_LOCAL_INVALID;
        statement_result_direct_local = minic_core_lower_direct_local_read(
            context,
            expression->value.statement_expression.result,
            &statement_result_local);
        statement_result_local_known =
            statement_result_direct_local &&
            minic_core_local_fact_get(
                context, statement_result_local, &statement_result_local_value);
        statement_known = core_const_eval_integer_with_locals(
            context, expression->value.statement_expression.result, &operand_value);
        if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "LOCAL_FACT_STMT function=%s expression=%zu result=%zu result_kind=%d result_vcat=%d direct_local=%d local=%zu local_known=%d local_bits=%" PRIu64 " known=%d bits=%" PRIu64 "\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          (size_t)expression_id,
                          (size_t)expression->value.statement_expression.result,
                          statement_result_expression != NULL
                              ? (int)statement_result_expression->kind
                              : -1,
                          statement_result_expression != NULL
                              ? (int)statement_result_expression->value_category
                              : -1,
                          statement_result_direct_local ? 1 : 0,
                          statement_result_direct_local
                              ? (size_t)statement_result_local
                              : (size_t)SIZE_MAX,
                          statement_result_local_known ? 1 : 0,
                          statement_result_local_known
                              ? statement_result_local_value.bits
                              : UINT64_C(0),
                          statement_known ? 1 : 0,
                          statement_known ? operand_value.bits : UINT64_C(0));
        }
        if (!statement_known) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &operand_value,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        const MinicExpression *right = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        uint64_t left_bits;
        uint64_t right_bits;
        MinicCoreGlobalAddressFact left_global;
        MinicCoreGlobalAddressFact right_global;
        MinicFunctionId left_function;
        MinicFunctionId right_function;
        bool predicate;
        if (left != NULL && right != NULL &&
            (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
             expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) &&
            minic_type_is_pointer(left->type) && minic_type_is_pointer(right->type) &&
            core_cfg_specialized_function_symbol(
                context, expression->value.binary.left, 0U, &left_function) &&
            core_cfg_specialized_function_symbol(
                context, expression->value.binary.right, 0U, &right_function)) {
            predicate = left_function == right_function;
            if (expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) {
                predicate = !predicate;
            }
            value->type = expression->type;
            value->bits = predicate ? 1U : 0U;
            return true;
        }
        if (left != NULL && right != NULL &&
            minic_type_is_pointer(left->type) && minic_type_is_pointer(right->type) &&
            minic_type_equal(left->type, right->type) &&
            core_cfg_global_address_fact(context,
                expression->value.binary.left, 0U, &left_global) &&
            core_cfg_global_address_fact(context,
                expression->value.binary.right, 0U, &right_global) &&
            left_global.object_id == right_global.object_id) {
            switch (expression->value.binary.operator_kind) {
            case MINIC_BINARY_EQUAL:
                predicate = left_global.byte_offset == right_global.byte_offset;
                break;
            case MINIC_BINARY_NOT_EQUAL:
                predicate = left_global.byte_offset != right_global.byte_offset;
                break;
            case MINIC_BINARY_LESS:
                predicate = left_global.byte_offset < right_global.byte_offset;
                break;
            case MINIC_BINARY_LESS_EQUAL:
                predicate = left_global.byte_offset <= right_global.byte_offset;
                break;
            case MINIC_BINARY_GREATER:
                predicate = left_global.byte_offset > right_global.byte_offset;
                break;
            case MINIC_BINARY_GREATER_EQUAL:
                predicate = left_global.byte_offset >= right_global.byte_offset;
                break;
            default: return false;
            }
            value->type = expression->type;
            value->bits = predicate ? 1U : 0U;
            return true;
        }
        /* A strong function designator has a non-null address.  This is
         * deliberately not a generic pointer-nonnull rule: GNU undefined
         * weak function references can legitimately resolve to NULL.  The
         * other operand must be a proven null pointer constant (or a pure
         * local fact), and no effectful operand may be discarded. */
        if (left != NULL && right != NULL &&
            (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
             expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) &&
            minic_type_is_pointer(left->type) && minic_type_is_pointer(right->type) &&
            minic_core_cfg_pure_call_argument(context, expression_id, 0U)) {
            MinicFunctionId symbol = MINIC_FUNCTION_INVALID;
            bool null_other = false;
            if (core_cfg_specialized_function_symbol(
                    context, expression->value.binary.left, 0U, &symbol)) {
                null_other = core_expression_known_null_pointer_depth(
                    context, expression->value.binary.right, 0U);
            } else if (core_cfg_specialized_function_symbol(
                           context, expression->value.binary.right, 0U, &symbol)) {
                null_other = core_expression_known_null_pointer_depth(
                    context, expression->value.binary.left, 0U);
            }
            if (null_other && symbol < context->body->program->function_count &&
                !context->body->program->functions[symbol].is_weak) {
                predicate = expression->value.binary.operator_kind ==
                    MINIC_BINARY_NOT_EQUAL;
                value->type = expression->type;
                value->bits = predicate ? 1U : 0U;
                return true;
            }
        }
        if (left != NULL && right != NULL &&
            minic_type_is_pointer(left->type) && minic_type_is_pointer(right->type) &&
            core_cfg_const_pointer_bits(
                context, expression->value.binary.left, 0U, &left_bits) &&
            core_cfg_const_pointer_bits(
                context, expression->value.binary.right, 0U, &right_bits)) {
            switch (expression->value.binary.operator_kind) {
            case MINIC_BINARY_EQUAL: predicate = left_bits == right_bits; break;
            case MINIC_BINARY_NOT_EQUAL: predicate = left_bits != right_bits; break;
            case MINIC_BINARY_LESS: predicate = left_bits < right_bits; break;
            case MINIC_BINARY_LESS_EQUAL: predicate = left_bits <= right_bits; break;
            case MINIC_BINARY_GREATER: predicate = left_bits > right_bits; break;
            case MINIC_BINARY_GREATER_EQUAL: predicate = left_bits >= right_bits; break;
            default: return false;
            }
            value->type = expression->type;
            value->bits = predicate ? 1U : 0U;
            return true;
        }
    }

    /* The left operand of &&/|| is evaluated in C, so reverse short-circuiting
       is safe only when that operand is side-effect free.  This closes CONFIG
       stubs such as mapping->host && (mapping->host->i_flags & 0). */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR)) {
        MinicConstValue right_value;
        bool right_is_zero;
        if (core_const_eval_integer_with_locals(
                context, expression->value.binary.right, &right_value) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &right_value,
                                      &right_is_zero) &&
            minic_core_cfg_pure_call_argument(
                context, expression->value.binary.left, 0U) &&
            ((expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND &&
              right_is_zero) ||
             (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR &&
              !right_is_zero))) {
            value->type = expression->type;
            value->bits = right_is_zero ? 0U : 1U;
            return true;
        }
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR)) {
        MinicConstValue left_value;
        MinicConstValue right_value;
        bool left_is_zero;
        bool right_is_zero;
        bool result;

        if (!core_const_eval_integer_with_locals(
                context, expression->value.binary.left, &left_value) ||
            !minic_const_value_is_zero(context->body->program,
                                       context->target,
                                       &left_value,
                                       &left_is_zero)) {
            return false;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND &&
            left_is_zero) {
            value->type = expression->type;
            value->bits = 0U;
            return true;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR &&
            !left_is_zero) {
            value->type = expression->type;
            value->bits = 1U;
            return true;
        }
        if (!core_const_eval_integer_with_locals(
                context, expression->value.binary.right, &right_value) ||
            !minic_const_value_is_zero(context->body->program,
                                       context->target,
                                       &right_value,
                                       &right_is_zero)) {
            return false;
        }
        result = !right_is_zero;
        value->type = expression->type;
        value->bits = result ? 1U : 0U;
        return true;
    }

    /* Linux min/max/clamp signedness masks combine optimizer-known local facts
       through ?:, integer addition and bitwise AND.  Compose only these narrow
       integer forms; the fact lifetime/escape rules remain unchanged. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {
        MinicConstValue condition_value;
        MinicConstValue selected_value;
        bool condition_is_zero;
        MinicExpressionId selected_id;

        if (!core_const_eval_integer_with_locals(
                context, expression->value.conditional.condition, &condition_value) ||
            !minic_const_value_is_zero(context->body->program,
                                       context->target,
                                       &condition_value,
                                       &condition_is_zero)) {
            return false;
        }
        selected_id = condition_is_zero ? expression->value.conditional.when_false
                                        : expression->value.conditional.when_true;
        if (!core_const_eval_integer_with_locals(context, selected_id, &selected_value)) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &selected_value,
                                                 expression->type,
                                                 value);
    }
    /* M189_CFG_ANNIHILATOR_PRIORITY: the unknown operand must not be
       rejected by strict two-constant arithmetic before the existing pure
       X&0 / X*0 identity gets a chance to prove the result. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY)) {
        MinicConstValue known;
        bool is_zero;

        if (core_const_eval_integer_with_locals(
                context, expression->value.binary.left, &known) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &known,
                                      &is_zero) &&
            is_zero &&
            minic_core_cfg_pure_call_argument(
                context, expression->value.binary.right, 0U)) {
            value->type = expression->type;
            value->bits = 0U;
            return true;
        }
        if (core_const_eval_integer_with_locals(
                context, expression->value.binary.right, &known) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &known,
                                      &is_zero) &&
            is_zero &&
            minic_core_cfg_pure_call_argument(
                context, expression->value.binary.left, 0U)) {
            value->type = expression->type;
            value->bits = 0U;
            return true;
        }
    }
    /* M194_KBUILD_BIT_MASK_REACHABILITY: a nonconstant ternary such as
     * (runtime_condition ? VM_EXEC : 0) still cannot set any bit outside
     * VM_EXEC.  Linux combines this with constant VM flags and masks against
     * a disjoint range in BUILD_BUG_ON.  Prove only the all-zero result, and
     * only when dropping the full original expression preserves effects. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND &&
        minic_core_cfg_pure_call_argument(context, expression_id, 0U) &&
        core_cfg_possible_one_bits(context, expression_id, 0U) == 0U) {
        value->type = expression->type;
        value->bits = 0U;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE ||
         expression->value.binary.operator_kind == MINIC_BINARY_REMAINDER ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_XOR ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicConstValue left;
        MinicConstValue right;
        MinicConstValue left_common;
        MinicConstValue right_common;
        MinicType common_type;
        uint64_t result_bits;
        unsigned int width;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !minic_type_is_integer(left_expression->type) ||
            !minic_type_is_integer(right_expression->type) ||
            !core_const_eval_integer_with_locals(context,
                                                expression->value.binary.left,
                                                &left) ||
            !core_const_eval_integer_with_locals(context,
                                                expression->value.binary.right,
                                                &right) ||
            !minic_target_info_integer_common_for_program(context->target,
                                                          context->body->program,
                                                          left_expression->type,
                                                          right_expression->type,
                                                          &common_type) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &left,
                                               common_type,
                                               &left_common) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &right,
                                               common_type,
                                               &right_common) ||
            !minic_target_info_integer_width(context->target,
                                             context->body->program,
                                             common_type,
                                             &width) ||
            width == 0U || width > 64U) {
            return false;
        }
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_SUBTRACT:
            result_bits = left_common.bits - right_common.bits;
            break;
        case MINIC_BINARY_MULTIPLY:
            result_bits = left_common.bits * right_common.bits;
            break;
        case MINIC_BINARY_DIVIDE:
        case MINIC_BINARY_REMAINDER:
            if (minic_type_is_signed_integer(common_type)) {
                int64_t ls;
                int64_t rs;
                int64_t minimum;
                if (!core_const_signed_value(context, &left_common, &ls) ||
                    !core_const_signed_value(context, &right_common, &rs) || rs == 0) {
                    return false;
                }
                minimum = width == 64U ? INT64_MIN : -(INT64_C(1) << (width - 1U));
                if (ls == minimum && rs == -1) {
                    return false;
                }
                result_bits = (uint64_t)(
                    expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE
                        ? ls / rs
                        : ls % rs);
            } else {
                if (right_common.bits == 0U) {
                    return false;
                }
                result_bits = expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE
                                  ? left_common.bits / right_common.bits
                                  : left_common.bits % right_common.bits;
            }
            break;
        case MINIC_BINARY_SHIFT_LEFT:
        case MINIC_BINARY_SHIFT_RIGHT: {
            uint64_t shift = right_common.bits;
            if (shift >= width) {
                return false;
            }
            if (expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT) {
                result_bits = left_common.bits << (unsigned int)shift;
            } else if (minic_type_is_signed_integer(common_type)) {
                int64_t ls;
                if (!core_const_signed_value(context, &left_common, &ls)) {
                    return false;
                }
                result_bits = (uint64_t)(ls >> (unsigned int)shift);
            } else {
                result_bits = left_common.bits >> (unsigned int)shift;
            }
            break;
        }
        case MINIC_BINARY_BITWISE_XOR:
            result_bits = left_common.bits ^ right_common.bits;
            break;
        case MINIC_BINARY_BITWISE_OR:
            result_bits = left_common.bits | right_common.bits;
            break;
        default:
            return false;
        }
        left_common.type = common_type;
        if (!core_cfg_normalize_integer(context,
                                        common_type,
                                        result_bits,
                                        &left_common.bits)) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &left_common,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicConstValue left;
        MinicConstValue right;
        MinicConstValue left_common;
        MinicConstValue right_common;
        MinicConstValue result_common;
        MinicType common_type;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !minic_type_is_integer(left_expression->type) ||
            !minic_type_is_integer(right_expression->type) ||
            !core_const_eval_integer_with_locals(
                context, expression->value.binary.left, &left) ||
            !core_const_eval_integer_with_locals(
                context, expression->value.binary.right, &right) ||
            !minic_target_info_integer_common_for_program(context->target,
                                                          context->body->program,
                                                          left_expression->type,
                                                          right_expression->type,
                                                          &common_type) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &left,
                                               common_type,
                                               &left_common) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &right,
                                               common_type,
                                               &right_common)) {
            return false;
        }
        result_common.type = common_type;
        result_common.bits = expression->value.binary.operator_kind == MINIC_BINARY_ADD
                                 ? left_common.bits + right_common.bits
                                 : left_common.bits & right_common.bits;
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &result_common,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_PLUS ||
         expression->value.unary.operator_kind == MINIC_UNARY_NEGATE ||
         expression->value.unary.operator_kind == MINIC_UNARY_BITWISE_NOT)) {
        MinicConstValue operand;
        uint64_t bits;

        if (!core_const_eval_integer_with_locals(
                context, expression->value.unary.operand, &operand) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &operand,
                                               expression->type,
                                               &operand) ||
            !core_cfg_normalize_integer(context, expression->type, operand.bits, &bits)) {
            return false;
        }
        if (expression->value.unary.operator_kind == MINIC_UNARY_NEGATE) {
            if (minic_type_is_signed_integer(expression->type)) {
                int64_t signed_value;
                unsigned int width;
                if (!core_const_signed_value(context, &operand, &signed_value) ||
                    !minic_target_info_integer_width(context->target,
                                                     context->body->program,
                                                     expression->type,
                                                     &width) ||
                    (width == 64U && signed_value == INT64_MIN) ||
                    (width < 64U && signed_value == -(INT64_C(1) << (width - 1U)))) {
                    return false;
                }
            }
            bits = UINT64_C(0) - bits;
        } else if (expression->value.unary.operator_kind == MINIC_UNARY_BITWISE_NOT) {
            bits = ~bits;
        }
        value->type = expression->type;
        return core_cfg_normalize_integer(context, expression->type, bits, &value->bits);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        const MinicExpression *right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        bool left_range = minic_core_lower_boolean_range_expression(
            context, expression->value.binary.left, 0U);
        bool right_range = minic_core_lower_boolean_range_expression(
            context, expression->value.binary.right, 0U);

        if (left_expression != NULL && right_expression != NULL &&
            left_range != right_range &&
            /* A range proof says only what an expression returns, not that
             * evaluating it is optional. This condition also rejects calls,
             * assignments, increments and volatile reads anywhere inside
             * the comparison, including its boolean-valued operand. */
            minic_core_cfg_pure_call_argument(context, expression_id, 0U)) {
            MinicExpressionId constant_id = left_range
                                                ? expression->value.binary.right
                                                : expression->value.binary.left;
            MinicConstValue constant_value;
            MinicConstValue constant_common;
            MinicConstValue range_zero;
            MinicConstValue range_one;
            MinicConstValue zero_common;
            MinicConstValue one_common;
            MinicType range_type = left_range ? left_expression->type
                                              : right_expression->type;
            MinicType constant_type = left_range ? right_expression->type
                                                 : left_expression->type;
            MinicType common_type;
            bool predicate_zero;
            bool predicate_one;

            range_zero.type = range_type;
            range_zero.bits = 0U;
            range_one.type = range_type;
            range_one.bits = 1U;
            if (core_const_eval_integer_with_locals(
                    context, constant_id, &constant_value) &&
                minic_target_info_integer_common_for_program(
                    context->target,
                    context->body->program,
                    range_type,
                    constant_type,
                    &common_type) &&
                minic_const_value_convert_integer(
                    context->body->program,
                    context->target,
                    &constant_value,
                    common_type,
                    &constant_common) &&
                minic_const_value_convert_integer(
                    context->body->program,
                    context->target,
                    &range_zero,
                    common_type,
                    &zero_common) &&
                minic_const_value_convert_integer(
                    context->body->program,
                    context->target,
                    &range_one,
                    common_type,
                    &one_common) &&
                core_boolean_compare_values(
                    context,
                    expression->value.binary.operator_kind,
                    common_type,
                    left_range ? &zero_common : &constant_common,
                    left_range ? &constant_common : &zero_common,
                    &predicate_zero) &&
                core_boolean_compare_values(
                    context,
                    expression->value.binary.operator_kind,
                    common_type,
                    left_range ? &one_common : &constant_common,
                    left_range ? &constant_common : &one_common,
                    &predicate_one) &&
                predicate_zero == predicate_one) {
                value->type = expression->type;
                value->bits = predicate_zero ? 1U : 0U;
                return true;
            }
        }
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicConstValue left;
        MinicConstValue right;
        MinicConstValue left_common;
        MinicConstValue right_common;
        MinicType common_type;
        bool predicate;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !minic_type_is_integer(left_expression->type) ||
            !minic_type_is_integer(right_expression->type) ||
            !core_const_eval_integer_with_locals(
                context, expression->value.binary.left, &left) ||
            !core_const_eval_integer_with_locals(
                context, expression->value.binary.right, &right) ||
            !minic_target_info_integer_common_for_program(context->target,
                                                          context->body->program,
                                                          left_expression->type,
                                                          right_expression->type,
                                                          &common_type) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &left,
                                               common_type,
                                               &left_common) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &right,
                                               common_type,
                                               &right_common)) {
            return false;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
            expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) {
            predicate = left_common.bits == right_common.bits;
            if (expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) {
                predicate = !predicate;
            }
        } else if (minic_type_is_signed_integer(common_type)) {
            int64_t left_signed;
            int64_t right_signed;
            if (!core_const_signed_value(context, &left_common, &left_signed) ||
                !core_const_signed_value(context, &right_common, &right_signed)) {
                return false;
            }
            switch (expression->value.binary.operator_kind) {
            case MINIC_BINARY_LESS: predicate = left_signed < right_signed; break;
            case MINIC_BINARY_LESS_EQUAL: predicate = left_signed <= right_signed; break;
            case MINIC_BINARY_GREATER: predicate = left_signed > right_signed; break;
            case MINIC_BINARY_GREATER_EQUAL: predicate = left_signed >= right_signed; break;
            default: return false;
            }
        } else {
            switch (expression->value.binary.operator_kind) {
            case MINIC_BINARY_LESS: predicate = left_common.bits < right_common.bits; break;
            case MINIC_BINARY_LESS_EQUAL: predicate = left_common.bits <= right_common.bits; break;
            case MINIC_BINARY_GREATER: predicate = left_common.bits > right_common.bits; break;
            case MINIC_BINARY_GREATER_EQUAL: predicate = left_common.bits >= right_common.bits; break;
            default: return false;
            }
        }
        value->type = expression->type;
        value->bits = predicate ? 1U : 0U;
        return true;
    }
    return false;
}
