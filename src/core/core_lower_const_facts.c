#include "core/core_lower_internal.h"
#include "core/local_facts.h"
#include "core/cfg_constant_calls.h"
#include "core/source_loop_shapes.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
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
    {
        const MinicExpression *read = expression;
        const MinicExpression *subscript;
        const MinicExpression *base;
        const MinicGlobalObject *object;
        MinicConstValue index_value;
        MinicConstValue element_value;
        MinicExpressionId base_id;
        unsigned int unwrap_depth;

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
                    base->kind == MINIC_EXPRESSION_CONVERSION ||
                    base->kind == MINIC_EXPRESSION_LVALUE_READ ||
                    base->kind == MINIC_EXPRESSION_ADDRESS_OF) {
                    base_id = base->value.unary.operand;
                    base = minic_c0_program_expression(context->body->program, base_id);
                    continue;
                }
                break;
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
