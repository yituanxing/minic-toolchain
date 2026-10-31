#include "compiler/inline_specialize.h"
#include "compiler/inline_specialize_internal.h"
#include "frontend/const_eval.h"
#include "frontend/function_body.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compiler AST pass: boolean-domain cache and range specialization.
 * Function bodies remain from the validated monolithic optimizer. */
static bool minic_inline_boolean_domain_value(
    const MinicC0Program *program,
    MinicExpressionId expression_id,
    unsigned int depth) {
    const MinicExpression *expression;

    if (program == NULL || depth > 32U) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (minic_type_is_bool_integer(expression->type)) {
        return true;
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
        return expression->value.integer_value == 0 ||
               expression->value.integer_value == 1;
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_LVALUE_READ:
        return minic_inline_boolean_domain_value(
            program, expression->value.unary.operand, depth + 1U);
    case MINIC_EXPRESSION_UNARY:
        if (expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
            return true;
        }
        if (expression->value.unary.operator_kind == MINIC_UNARY_PLUS) {
            return minic_inline_boolean_domain_value(
                program, expression->value.unary.operand, depth + 1U);
        }
        return false;
    case MINIC_EXPRESSION_BINARY:
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_EQUAL:
        case MINIC_BINARY_NOT_EQUAL:
        case MINIC_BINARY_LESS:
        case MINIC_BINARY_LESS_EQUAL:
        case MINIC_BINARY_GREATER:
        case MINIC_BINARY_GREATER_EQUAL:
        case MINIC_BINARY_LOGICAL_AND:
        case MINIC_BINARY_LOGICAL_OR:
            return true;
        case MINIC_BINARY_COMMA:
            return minic_inline_boolean_domain_value(
                program, expression->value.binary.right, depth + 1U);
        default:
            return false;
        }
    case MINIC_EXPRESSION_CONDITIONAL:
        return minic_inline_boolean_domain_value(
                   program, expression->value.conditional.when_true, depth + 1U) &&
               minic_inline_boolean_domain_value(
                   program, expression->value.conditional.when_false, depth + 1U);
    case MINIC_EXPRESSION_ASSIGNMENT:
        return minic_inline_boolean_domain_value(
            program, expression->value.binary.right, depth + 1U);
    default:
        return false;
    }
}

static bool minic_inline_direct_local_target(
    const MinicC0Program *program,
    MinicExpressionId expression_id,
    MinicLocalId *local_id) {
    const MinicExpression *expression;

    if (program == NULL || local_id == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL) {
        return false;
    }
    *local_id = expression->value.local_id;
    return true;
}

static bool minic_inline_local_is_parameter(
    const MinicC0Program *program,
    MinicLocalId local_id) {
    size_t function_index;

    if (program == NULL) {
        return false;
    }
    for (function_index = 0U; function_index < program->function_count;
         ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        if (!function->is_defined || function->parameter_count == 0U) {
            continue;
        }
        if (local_id >= function->local_begin &&
            local_id - function->local_begin < function->parameter_count) {
            return true;
        }
    }
    return false;
}

/*
 * Conservatively recognize ordinary integer locals whose complete direct-write
 * set is confined to {0,1}.  This covers Linux patterns such as an unsigned
 * int flag initialized to 0 and conditionally assigned 1 before being passed
 * through always-inline helpers.  Reject address escapes, compound updates,
 * increments/decrements and asm outputs so the fact is never inferred across
 * an unknown write.
 */
static bool minic_inline_local_boolean_domain_legacy(
    const MinicC0Program *program,
    MinicLocalId local_id) {
    bool saw_assignment = false;
    size_t statement_index;
    size_t expression_index;
    size_t asm_index;

    if (program == NULL || local_id >= program->local_count ||
        !minic_type_is_integer(program->locals[local_id].type) ||
        minic_inline_local_is_parameter(program, local_id)) {
        return false;
    }

    for (statement_index = 0U; statement_index < program->statement_count;
         ++statement_index) {
        const MinicStatement *statement = &program->statements[statement_index];
        MinicLocalId target_local = MINIC_LOCAL_INVALID;
        if (!minic_inline_direct_local_target(
                program, statement->target_expression, &target_local) ||
            target_local != local_id) {
            continue;
        }
        if (statement->kind != MINIC_STATEMENT_ASSIGN ||
            statement->expression == MINIC_EXPRESSION_INVALID ||
            !minic_inline_boolean_domain_value(
                program, statement->expression, 0U)) {
            return false;
        }
        saw_assignment = true;
    }

    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        const MinicExpression *expression = &program->expressions[expression_index];
        MinicLocalId target_local = MINIC_LOCAL_INVALID;

        if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF &&
            minic_inline_direct_local_target(
                program, expression->value.unary.operand, &target_local) &&
            target_local == local_id) {
            return false;
        }
        if (expression->kind == MINIC_EXPRESSION_UNARY &&
            (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT) &&
            minic_inline_direct_local_target(
                program, expression->value.unary.operand, &target_local) &&
            target_local == local_id) {
            return false;
        }
        if ((expression->kind == MINIC_EXPRESSION_ASSIGNMENT ||
             expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) &&
            minic_inline_direct_local_target(
                program, expression->value.binary.left, &target_local) &&
            target_local == local_id) {
            if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT ||
                !minic_inline_boolean_domain_value(
                    program, expression->value.binary.right, 0U)) {
                return false;
            }
            saw_assignment = true;
        }
    }

    for (asm_index = 0U; asm_index < program->inline_asm_count; ++asm_index) {
        const MinicInlineAsm *inline_asm = &program->inline_asms[asm_index];
        size_t output_index;
        for (output_index = 0U; output_index < inline_asm->output_count;
             ++output_index) {
            MinicLocalId output_local = MINIC_LOCAL_INVALID;
            if (minic_inline_direct_local_target(
                    program,
                    inline_asm->outputs[output_index].expression,
                    &output_local) &&
                output_local == local_id) {
                return false;
            }
        }
    }

    return saw_assignment;
}



typedef struct MinicInlineLocalBooleanDomainCache {
    const MinicC0Program *program;
    bool *values;
    size_t local_count;
    size_t statement_count;
    size_t expression_count;
    size_t inline_asm_count;
} MinicInlineLocalBooleanDomainCache;

/* Local to one AST boolean specialization invocation. */
typedef struct MinicInlineBooleanDomainPassState {
    MinicInlineLocalBooleanDomainCache cache;
    MinicInlineBooleanDomainStats stats;
} MinicInlineBooleanDomainPassState;

void minic_inline_boolean_domain_perf_dump(
    FILE *out, const MinicInlineBooleanDomainStats *stats) {
    if (out == NULL || stats == NULL) return;
    (void)fprintf(out,
                  "MINIC_BOOLEAN_DOMAIN queries=%zu builds=%zu fallbacks=%zu\n",
                  stats->queries, stats->builds, stats->fallbacks);
}

static bool minic_inline_build_local_boolean_domain_cache(
    const MinicC0Program *program, MinicInlineBooleanDomainPassState *state) {
    bool *values = NULL;
    bool *parameter = NULL;
    bool *saw_assignment = NULL;
    bool *invalid = NULL;
    size_t function_index;
    size_t statement_index;
    size_t expression_index;
    size_t asm_index;
    size_t local_index;

    (void)minic_inline_local_is_parameter;
    if (program == NULL ||
        program->local_count > SIZE_MAX / sizeof(bool)) {
        return false;
    }
    values = (bool *)calloc(program->local_count == 0U ? 1U : program->local_count,
                            sizeof(*values));
    parameter = (bool *)calloc(program->local_count == 0U ? 1U : program->local_count,
                               sizeof(*parameter));
    saw_assignment =
        (bool *)calloc(program->local_count == 0U ? 1U : program->local_count,
                       sizeof(*saw_assignment));
    invalid = (bool *)calloc(program->local_count == 0U ? 1U : program->local_count,
                             sizeof(*invalid));
    if (values == NULL || parameter == NULL || saw_assignment == NULL ||
        invalid == NULL) {
        free(values);
        free(parameter);
        free(saw_assignment);
        free(invalid);
        return false;
    }

    for (function_index = 0U; function_index < program->function_count;
         ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        size_t parameter_index;
        if (!function->is_defined || function->local_begin > program->local_count ||
            function->parameter_count > program->local_count - function->local_begin) {
            continue;
        }
        for (parameter_index = 0U; parameter_index < function->parameter_count;
             ++parameter_index) {
            parameter[function->local_begin + parameter_index] = true;
        }
    }

    for (statement_index = 0U; statement_index < program->statement_count;
         ++statement_index) {
        const MinicStatement *statement = &program->statements[statement_index];
        MinicLocalId target_local = MINIC_LOCAL_INVALID;
        if (!minic_inline_direct_local_target(
                program, statement->target_expression, &target_local) ||
            target_local >= program->local_count) {
            continue;
        }
        if (statement->kind != MINIC_STATEMENT_ASSIGN ||
            statement->expression == MINIC_EXPRESSION_INVALID ||
            !minic_inline_boolean_domain_value(
                program, statement->expression, 0U)) {
            invalid[target_local] = true;
        } else {
            saw_assignment[target_local] = true;
        }
    }

    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        const MinicExpression *expression = &program->expressions[expression_index];
        MinicLocalId target_local = MINIC_LOCAL_INVALID;

        if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF &&
            minic_inline_direct_local_target(
                program, expression->value.unary.operand, &target_local) &&
            target_local < program->local_count) {
            invalid[target_local] = true;
            continue;
        }
        if (expression->kind == MINIC_EXPRESSION_UNARY &&
            (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT) &&
            minic_inline_direct_local_target(
                program, expression->value.unary.operand, &target_local) &&
            target_local < program->local_count) {
            invalid[target_local] = true;
            continue;
        }
        if ((expression->kind == MINIC_EXPRESSION_ASSIGNMENT ||
             expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) &&
            minic_inline_direct_local_target(
                program, expression->value.binary.left, &target_local) &&
            target_local < program->local_count) {
            if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT ||
                !minic_inline_boolean_domain_value(
                    program, expression->value.binary.right, 0U)) {
                invalid[target_local] = true;
            } else {
                saw_assignment[target_local] = true;
            }
        }
    }

    for (asm_index = 0U; asm_index < program->inline_asm_count; ++asm_index) {
        const MinicInlineAsm *inline_asm = &program->inline_asms[asm_index];
        size_t output_index;
        for (output_index = 0U; output_index < inline_asm->output_count;
             ++output_index) {
            MinicLocalId output_local = MINIC_LOCAL_INVALID;
            if (minic_inline_direct_local_target(
                    program,
                    inline_asm->outputs[output_index].expression,
                    &output_local) &&
                output_local < program->local_count) {
                invalid[output_local] = true;
            }
        }
    }

    for (local_index = 0U; local_index < program->local_count; ++local_index) {
        values[local_index] =
            minic_type_is_integer(program->locals[local_index].type) &&
            !parameter[local_index] &&
            saw_assignment[local_index] &&
            !invalid[local_index];
    }

    free(parameter);
    free(saw_assignment);
    free(invalid);
    free(state->cache.values);
    state->cache.program = program;
    state->cache.values = values;
    state->cache.local_count = program->local_count;
    state->cache.statement_count = program->statement_count;
    state->cache.expression_count = program->expression_count;
    state->cache.inline_asm_count = program->inline_asm_count;
    state->stats.builds += 1U;
    return true;
}


/* MINIC_BOOLEAN_DOMAIN_SCOPED_NATIVE_V1: certified on 500 real Linux inputs. */
static bool minic_inline_local_boolean_domain(
    const MinicC0Program *program,
    MinicLocalId local_id, MinicInlineBooleanDomainPassState *state) {
    if (program == NULL || local_id >= program->local_count) {
        return false;
    }
    state->stats.queries += 1U;
    /* An appended clone may grow the underlying arenas without changing
     * the Program address. All arenas referenced by write scanning matter. */
    if (state->cache.program != program ||
        state->cache.local_count != program->local_count ||
        state->cache.statement_count != program->statement_count ||
        state->cache.expression_count != program->expression_count ||
        state->cache.inline_asm_count != program->inline_asm_count ||
        state->cache.values == NULL) {
        if (!minic_inline_build_local_boolean_domain_cache(program, state)) {
            state->stats.fallbacks += 1U;
            return minic_inline_local_boolean_domain_legacy(program, local_id);
        }
    }
    return state->cache.values[local_id];
}

static bool minic_inline_boolean_range_argument(
    const MinicC0Program *program,
    const MinicFunction *caller,
    MinicExpressionId expression_id,
    bool *used_caller_fact,
    unsigned int depth, MinicInlineBooleanDomainPassState *state) {
    const MinicExpression *expression;

    if (program == NULL || used_caller_fact == NULL || depth > 32U) {
        return false;
    }
    *used_caller_fact = false;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (minic_type_is_bool_integer(expression->type)) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return minic_inline_boolean_range_argument(
            program,
            caller,
            expression->value.unary.operand,
            used_caller_fact,
            depth + 1U, state);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        MinicLocalId local_id = expression->value.local_id;

        if (minic_inline_local_boolean_domain(program, local_id, state)) {
            return true;
        }
        if (caller != NULL && caller->is_integer_specialization &&
            caller->specialization_source < program->function_count) {
            const MinicFunction *source =
                &program->functions[caller->specialization_source];
            size_t parameter_index;

            if (local_id < source->local_begin) {
                return false;
            }
            parameter_index = local_id - source->local_begin;
            if (parameter_index >= source->parameter_count ||
                parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
                !caller->specialization_boolean_range_known[parameter_index]) {
                return false;
            }
            *used_caller_fact = true;
            return true;
        }
    }
    return false;
}

static void minic_inline_boolean_copy_existing_facts(
    const MinicFunction *current,
    size_t parameter_count,
    bool *integer_known,
    uint64_t *integer_bits,
    bool *symbolic_known,
    MinicExpressionId *symbolic_expression,
    bool *boolean_range_known) {
    size_t i;

    (void)memset(integer_known, 0,
                 MINIC_MAX_FUNCTION_PARAMETERS * sizeof(*integer_known));
    (void)memset(integer_bits, 0,
                 MINIC_MAX_FUNCTION_PARAMETERS * sizeof(*integer_bits));
    (void)memset(symbolic_known, 0,
                 MINIC_MAX_FUNCTION_PARAMETERS * sizeof(*symbolic_known));
    (void)memset(boolean_range_known, 0,
                 MINIC_MAX_FUNCTION_PARAMETERS * sizeof(*boolean_range_known));
    for (i = 0U; i < MINIC_MAX_FUNCTION_PARAMETERS; ++i) {
        symbolic_expression[i] = MINIC_EXPRESSION_INVALID;
    }
    if (current == NULL || !current->is_integer_specialization) {
        return;
    }
    for (i = 0U; i < parameter_count; ++i) {
        integer_known[i] = current->specialization_integer_known[i];
        integer_bits[i] = current->specialization_integer_bits[i];
        symbolic_known[i] = current->specialization_symbolic_known[i];
        symbolic_expression[i] = current->specialization_symbolic_expression[i];
        boolean_range_known[i] =
            current->specialization_boolean_range_known[i];
    }
}

static bool minic_inline_boolean_range_variant_matches(
    const MinicFunction *candidate,
    const MinicFunction *source,
    const MinicFunction *caller,
    MinicFunctionId source_id,
    const bool *integer_known,
    const uint64_t *integer_bits,
    const bool *symbolic_known,
    const MinicExpressionId *symbolic_expression,
    const bool *boolean_range_known,
    size_t parameter_count) {
    size_t i;

    if (candidate == NULL || !candidate->is_integer_specialization ||
        candidate->specialization_source != source_id ||
        candidate->parameter_count != parameter_count ||
        !minic_inline_specialization_section_matches(candidate, source, caller)) {
        return false;
    }
    for (i = 0U; i < parameter_count; ++i) {
        if (candidate->specialization_integer_known[i] != integer_known[i] ||
            (integer_known[i] &&
             candidate->specialization_integer_bits[i] != integer_bits[i]) ||
            candidate->specialization_symbolic_known[i] != symbolic_known[i] ||
            (symbolic_known[i] &&
             candidate->specialization_symbolic_expression[i] != symbolic_expression[i]) ||
            candidate->specialization_boolean_range_known[i] !=
                boolean_range_known[i]) {
            return false;
        }
    }
    return true;
}

static bool minic_add_inline_boolean_range_specialization(
    MinicC0Program *program,
    MinicFunctionId source_id,
    const MinicFunction *caller,
    const bool *integer_known,
    const uint64_t *integer_bits,
    const bool *symbolic_known,
    const MinicExpressionId *symbolic_expression,
    const bool *boolean_range_known,
    MinicFunctionId *specialized_id) {
    MinicFunction *specialized;
    size_t i;

    if (!minic_add_inline_symbolic_specialization(
            program,
            source_id,
            caller,
            integer_known,
            integer_bits,
            symbolic_known,
            symbolic_expression,
            specialized_id)) {
        return false;
    }
    specialized = &program->functions[*specialized_id];
    for (i = 0U; i < specialized->parameter_count; ++i) {
        specialized->specialization_boolean_range_known[i] =
            boolean_range_known[i];
    }
    return true;
}

static bool minic_specialize_inline_boolean_range_calls_for_pass(
    MinicC0Program *program,
    const MinicTargetInfo *target, MinicInlineBooleanDomainPassState *state) {
    size_t expression_index;
    size_t direct_clone_count = 0U;
    size_t transitive_clone_count = 0U;
    MinicFunctionId *initial_expression_owners = NULL;
    bool initial_owner_index_ready = false;

    if (program == NULL || target == NULL) {
        return false;
    }
    if (minic_inline_section_owner_index_required(program) &&
        program->expression_count != 0U &&
        program->expression_count <= SIZE_MAX / sizeof(*initial_expression_owners)) {
        initial_expression_owners = malloc(program->expression_count *
                                           sizeof(*initial_expression_owners));
        if (initial_expression_owners != NULL)
            initial_owner_index_ready = minic_perf_program_build_expression_source_owners(
                program, program->function_count, initial_expression_owners,
                program->expression_count);
    }
    if (minic_inline_section_owner_index_required(program) &&
        program->expression_count != 0U && !initial_owner_index_ready) {
        free(initial_expression_owners);
        (void)fprintf(stderr, "MINIC_INLINE_SECTION_OWNER_INDEX=FAIL boolean\n");
        return false;
    }

    /* First, calls whose source expression itself has _Bool type are safe to
       rewrite globally: that {0,1} property does not depend on caller state. */
    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        MinicExpression *call = &program->expressions[expression_index];
        MinicFunctionId current_id;
        MinicFunctionId source_id;
        MinicFunctionId variant_id = MINIC_FUNCTION_INVALID;
        MinicFunction current;
        MinicFunction source;
        const MinicFunction *caller = NULL;
        bool integer_known[MINIC_MAX_FUNCTION_PARAMETERS];
        uint64_t integer_bits[MINIC_MAX_FUNCTION_PARAMETERS];
        bool symbolic_known[MINIC_MAX_FUNCTION_PARAMETERS];
        MinicExpressionId symbolic_expression[MINIC_MAX_FUNCTION_PARAMETERS];
        bool boolean_range_known[MINIC_MAX_FUNCTION_PARAMETERS];
        bool adds_range = false;
        size_t i;
        size_t candidate_index;

        if (call->kind != MINIC_EXPRESSION_CALL ||
            call->value.call.function_id == MINIC_FUNCTION_INVALID) {
            continue;
        }
        current_id = call->value.call.function_id;
        if (current_id >= program->function_count) {
            continue;
        }
        current = program->functions[current_id];
        source_id = current.is_integer_specialization
                        ? current.specialization_source
                        : current_id;
        if (source_id >= program->function_count) {
            continue;
        }
        source = program->functions[source_id];
        if (initial_owner_index_ready &&
            initial_expression_owners[expression_index] < program->function_count)
            caller = &program->functions[initial_expression_owners[expression_index]];
        else if (current.is_integer_specialization)
            caller = &program->functions[current_id];
        if (!source.is_defined || !source.is_internal || !source.is_inline ||
            source.is_variadic || source.alias_target != MINIC_FUNCTION_INVALID ||
            source.parameter_count == 0U ||
            source.parameter_count != call->value.call.argument_count) {
            continue;
        }
        minic_inline_boolean_copy_existing_facts(
            &current,
            source.parameter_count,
            integer_known,
            integer_bits,
            symbolic_known,
            symbolic_expression,
            boolean_range_known);
        for (i = 0U; i < source.parameter_count; ++i) {
            const MinicExpression *argument;
            bool used_caller_fact = false;

            if (!minic_type_is_integer(source.parameter_types[i])) {
                continue;
            }
            argument = minic_c0_program_expression(
                program, minic_c0_call_argument(call, i));
            if (argument == NULL ||
                !minic_inline_boolean_range_argument(
                    program,
                    NULL,
                    minic_c0_call_argument(call, i),
                    &used_caller_fact,
                    0U, state)) {
                continue;
            }
            if (!boolean_range_known[i]) {
                boolean_range_known[i] = true;
                adds_range = true;
            }
        }
        if (!adds_range) {
            continue;
        }
        for (candidate_index = 0U; candidate_index < program->function_count;
             ++candidate_index) {
            if (minic_inline_boolean_range_variant_matches(
                    &program->functions[candidate_index],
                    &source, caller,
                    source_id,
                    integer_known,
                    integer_bits,
                    symbolic_known,
                    symbolic_expression,
                    boolean_range_known,
                    source.parameter_count)) {
                variant_id = candidate_index;
                break;
            }
        }
        if (variant_id == MINIC_FUNCTION_INVALID) {
            /* FIXME(MC-043): exhausting a speculative clone budget also
             * aborts ordinary legal C call sites here. The transitive
             * quota path below has the same failure contract. */
            if (direct_clone_count + transitive_clone_count >= 1024U ||
                !minic_add_inline_boolean_range_specialization(
                    program,
                    source_id,
                    caller,
                    integer_known,
                    integer_bits,
                    symbolic_known,
                    symbolic_expression,
                    boolean_range_known,
                    &variant_id)) {
                free(initial_expression_owners);
                return false;
            }
            direct_clone_count += 1U;
        }
        program->expressions[expression_index].value.call.function_id = variant_id;
    }
    free(initial_expression_owners);

    /* Then close range facts through already-specialized callers.  Do not
       rewrite the shared source AST here; Core lowering selects the matching
       variant using the actual caller specialization. */
    {
        size_t caller_index;
        /* All clones in this pass reuse canonical source FunctionBodies.
         * Build the original-expression ownership oracle and CALL-only
         * iteration list lazily: most TUs have no Boolean-range callers.
         * On OOM or invalid ownership, the original full walk is preserved.
         * Calls remain sorted by expression ID, maintaining clone order. */
        const size_t boolean_expression_limit = program->expression_count;
        const size_t boolean_function_limit = program->function_count;
        MinicFunctionId *boolean_owners = NULL;
        size_t *boolean_calls = NULL;
        size_t boolean_call_count = 0U;
        size_t boolean_index_builds = 0U;
        size_t boolean_caller_scans = 0U;
        size_t boolean_call_candidates = 0U;
        size_t boolean_skipped_other_owner = 0U;
        bool boolean_owner_ready = false;
        bool boolean_index_attempted = false;
        for (caller_index = 0U; caller_index < program->function_count;
             ++caller_index) {
            const MinicFunction caller = program->functions[caller_index];
            bool caller_has_range = false;
            size_t i;
            size_t nested_expression_index;

            if (!caller.is_integer_specialization ||
                caller.specialization_source >= program->function_count) {
                continue;
            }
            for (i = 0U; i < caller.parameter_count; ++i) {
                caller_has_range = caller_has_range ||
                    caller.specialization_boolean_range_known[i];
            }
            if (!caller_has_range) {
                continue;
            }
            boolean_caller_scans += 1U;
            if (!boolean_index_attempted) {
                boolean_index_attempted = true;
                boolean_index_builds += 1U;
                if (boolean_expression_limit != 0U &&
                    boolean_expression_limit <= SIZE_MAX / sizeof(*boolean_calls)) {
                    size_t call_id;
                    boolean_calls = (size_t *)malloc(
                        boolean_expression_limit * sizeof(*boolean_calls));
                    if (boolean_calls != NULL) {
                        for (call_id = 0U; call_id < boolean_expression_limit;
                             ++call_id) {
                            if (program->expressions[call_id].kind ==
                                MINIC_EXPRESSION_CALL) {
                                boolean_calls[boolean_call_count++] = call_id;
                            }
                        }
                    }
                }
                if (boolean_expression_limit != 0U &&
                    boolean_expression_limit <= SIZE_MAX / sizeof(*boolean_owners)) {
                    boolean_owners = (MinicFunctionId *)malloc(
                        boolean_expression_limit * sizeof(*boolean_owners));
                    if (boolean_owners != NULL) {
                        boolean_owner_ready =
                            minic_perf_program_build_expression_source_owners(
                                program, boolean_function_limit,
                                boolean_owners, boolean_expression_limit);
                        if (!boolean_owner_ready) {
                            free(boolean_owners);
                            boolean_owners = NULL;
                        }
                    }
                }
            }
            {
                const bool call_index_ready =
                    boolean_calls != NULL &&
                    program->expression_count == boolean_expression_limit;
                const size_t scan_limit = call_index_ready
                    ? boolean_call_count : program->expression_count;
                size_t scan_position;
                for (scan_position = 0U; scan_position < scan_limit; ++scan_position) {
                    const MinicExpression *nested;
                    nested_expression_index = call_index_ready
                        ? boolean_calls[scan_position] : scan_position;
                    nested = &program->expressions[nested_expression_index];
                MinicFunctionId current_id;
                MinicFunctionId source_id;
                MinicFunctionId variant_id = MINIC_FUNCTION_INVALID;
                MinicFunction current;
                MinicFunction source;
                bool integer_known[MINIC_MAX_FUNCTION_PARAMETERS];
                uint64_t integer_bits[MINIC_MAX_FUNCTION_PARAMETERS];
                bool symbolic_known[MINIC_MAX_FUNCTION_PARAMETERS];
                MinicExpressionId symbolic_expression[MINIC_MAX_FUNCTION_PARAMETERS];
                bool boolean_range_known[MINIC_MAX_FUNCTION_PARAMETERS];
                bool used_caller_fact = false;
                bool adds_range = false;
                bool needs_section_clone;
                size_t parameter_index;
                size_t candidate_index;

                if (nested->kind != MINIC_EXPRESSION_CALL ||
                    nested->value.call.function_id == MINIC_FUNCTION_INVALID) {
                    continue;
                }
                boolean_call_candidates += 1U;
                if (boolean_owner_ready &&
                    nested_expression_index < boolean_expression_limit) {
                    const MinicFunctionId owner =
                        boolean_owners[nested_expression_index];
                    if (owner != MINIC_FUNCTION_INVALID &&
                        owner < boolean_function_limit &&
                        owner != caller.specialization_source) {
                        boolean_skipped_other_owner += 1U;
                        continue;
                    }
                }
                current_id = nested->value.call.function_id;
                if (current_id >= program->function_count) {
                    continue;
                }
                current = program->functions[current_id];
                source_id = current.is_integer_specialization
                                ? current.specialization_source
                                : current_id;
                if (source_id >= program->function_count) {
                    continue;
                }
                source = program->functions[source_id];
                if (!source.is_defined || !source.is_internal || !source.is_inline ||
                    source.is_variadic || source.alias_target != MINIC_FUNCTION_INVALID ||
                    source.parameter_count == 0U ||
                    source.parameter_count != nested->value.call.argument_count) {
                    continue;
                }
                needs_section_clone = current.is_integer_specialization &&
                    !minic_inline_specialization_section_matches(&current, &source, &caller);
                minic_inline_boolean_copy_existing_facts(
                    &current,
                    source.parameter_count,
                    integer_known,
                    integer_bits,
                    symbolic_known,
                    symbolic_expression,
                    boolean_range_known);
                for (parameter_index = 0U;
                     parameter_index < source.parameter_count;
                     ++parameter_index) {
                    bool argument_used_caller_fact = false;
                    if (!minic_type_is_integer(source.parameter_types[parameter_index]) ||
                        !minic_inline_boolean_range_argument(
                            program,
                            &caller,
                            minic_c0_call_argument(nested, parameter_index),
                            &argument_used_caller_fact,
                            0U, state)) {
                        continue;
                    }
                    used_caller_fact =
                        used_caller_fact || argument_used_caller_fact;
                    if (!boolean_range_known[parameter_index]) {
                        boolean_range_known[parameter_index] = true;
                        adds_range = true;
                    }
                }
                if ((!used_caller_fact || !adds_range) && !needs_section_clone) {
                    continue;
                }
                for (candidate_index = 0U;
                     candidate_index < program->function_count;
                     ++candidate_index) {
                    if (minic_inline_boolean_range_variant_matches(
                            &program->functions[candidate_index],
                            &source, &caller,
                            source_id,
                            integer_known,
                            integer_bits,
                            symbolic_known,
                            symbolic_expression,
                            boolean_range_known,
                            source.parameter_count)) {
                        variant_id = candidate_index;
                        break;
                    }
                }
                if (variant_id == MINIC_FUNCTION_INVALID) {
                    if (direct_clone_count + transitive_clone_count >= 1024U ||
                        !minic_add_inline_boolean_range_specialization(
                            program,
                            source_id,
                            &caller,
                            integer_known,
                            integer_bits,
                            symbolic_known,
                            symbolic_expression,
                            boolean_range_known,
                            &variant_id)) {
                        free(boolean_calls);
                        free(boolean_owners);
                        return false;
                    }
                    transitive_clone_count += 1U;
                }
                }
            }
        }
        if (getenv("MINIC_BOOLEAN_TRANSITIVE_PERF") != NULL) {
            (void)fprintf(stderr,
                          "MINIC_BOOLEAN_TRANSITIVE_INDEX callers=%zu "
                          "index_builds=%zu call_entries=%zu candidates=%zu "
                          "skipped_other_owner=%zu calls_ready=%u "
                          "owners_ready=%u\n",
                          boolean_caller_scans, boolean_index_builds,
                          boolean_call_count, boolean_call_candidates,
                          boolean_skipped_other_owner,
                          (unsigned int)(boolean_calls != NULL),
                          (unsigned int)boolean_owner_ready);
        }
        free(boolean_calls);
        free(boolean_owners);
    }

    if (direct_clone_count != 0U || transitive_clone_count != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_BOOLEAN_RANGE_V0 direct=%zu transitive=%zu functions=%zu\n",
                      direct_clone_count,
                      transitive_clone_count,
                      program->function_count);
    }
    return true;
}


/* Own the cache over the complete pass, even on conservative early failure. */
bool minic_specialize_inline_boolean_range_calls(
    MinicC0Program *program, const MinicTargetInfo *target,
    MinicInlineBooleanDomainStats *stats_out) {
    MinicInlineBooleanDomainPassState state;
    bool success;
    (void)memset(&state, 0, sizeof(state));
    success = minic_specialize_inline_boolean_range_calls_for_pass(program, target, &state);
    if (stats_out != NULL) *stats_out = state.stats;
    free(state.cache.values);
    return success;
}
