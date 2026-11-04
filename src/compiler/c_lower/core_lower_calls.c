#include "compiler/c_lower/core_lower_internal.h"
#include "compiler/c_lower/core_lower_asm_internal.h"
#include "compiler/c_lower/local_facts.h"
#include "compiler/c_lower/cfg_constant_calls.h"
#include "compiler/inline_specialize_common.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Call semantics: direct/indirect and aggregate return share source-order
 * argument snapshots, independent of the architecture machine ABI. */

/* M85_RECORD_CALL_ARGUMENT: materialize a by-value record argument as a
   private Core object snapshot before later arguments are evaluated. */
static MinicCoreLowerStatus lower_record_call_argument_object(
    MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicType parameter_type,
    MinicCoreObjectId *object_id) {
    /* M128_RECORD_CALL_ARGUMENT_MATERIALIZATION: a by-value record argument
       consumes a record rvalue, not merely an already-address-backed lvalue.
       Make minic_core_lower_internal_record_materialized_address() the single aggregate producer
       owner here: it handles conditionals, compound literals, direct record
       returns, and falls back fail-closed for ordinary address-backed values.
       The private argument object below remains the evaluation-order snapshot
       consumed by the existing Core/ABI OBJECT call-argument path. */
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
    MinicType instruction_source_type = minic_type_void();
    MinicCoreValueId destination_address;
    MinicCoreValueId source_address;
    MinicCoreLowerStatus status;
    MinicType source_type;
    MinicType pointer_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || object_id == NULL || !minic_type_is_record(parameter_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    /* BATCH_V_TRANSPARENT_UNION_ARGUMENT: GNU transparent-union legality is
       already owned by frontend/Sema.  When a fixed argument is accepted via
       one of the union's pointer members, materialize the semantic union as a
       private Core object and initialize the matching member.  The existing
       OBJECT call argument then preserves the declared Core signature and the
       ordinary aggregate ABI path; Core does not re-define the language rule. */
    if (!minic_type_is_record(expression->type)) {
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreValueId object_address;
        MinicCoreValueId field_address;
        MinicCoreValueId field_value;
        MinicType abi_type;
        size_t field_index;

        if (!minic_c0_fixed_call_argument_compatible(
                context->body->program, parameter_type, expression_id) ||
            !minic_c0_fixed_parameter_abi_type(
                context->body->program, parameter_type, &abi_type) ||
            !core_memory_scalar_type(abi_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        record = minic_c0_program_record(context->body->program, parameter_type.record_id);
        if (record == NULL || !record->is_complete || !record->is_union ||
            !record->is_transparent_union || record->field_count == 0U) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        field = NULL;
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *candidate;

            candidate = minic_c0_record_field(record, field_index);
            if (candidate == NULL ||
                minic_c0_record_field_array_object_info(
                    context->body->program, parameter_type.record_id,
                    field_index, NULL) || candidate->is_bit_field ||
                !minic_type_is_pointer(candidate->type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            if (minic_c0_assignment_compatible(
                    context->body->program, candidate->type, expression_id)) {
                field = candidate;
                break;
            }
        }
        if (field == NULL) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = minic_core_lower_internal_lower_scalar_assignment_value(
            context, field->type, expression_id, &field_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (!minic_core_function_add_object(
                context->source, expression->span, parameter_type, object_id) ||
            !minic_type_pointer_to(parameter_type, &pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction_source_type = pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = *object_id;
        if (!minic_core_function_append_source_value_instruction(
                context->source, context->block_id, &instruction, instruction_source_type, &object_address)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = minic_core_lower_internal_append_field_address(context,
                                      expression->span,
                                      object_address,
                                      parameter_type.record_id,
                                      field_index,
                                      field->type,
                                      &field_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction_source_type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = field_address;
        instruction.value.store.stored_value = field_value;
        instruction.value.store.is_volatile = false;
        return minic_core_function_append_source_effect_instruction(
                   context->source, context->block_id, &instruction, instruction_source_type)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_type_unqualified(expression->type, &source_type) ||
        !minic_type_equal(source_type, parameter_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* BATCH_J_DIRECT_RECORD_CALL_ARGUMENT: a direct record-returning call
       already materializes its aggregate result into one private Core object.
       Passing that value immediately by value must reuse that exact result
       object rather than requiring the frontend expression to be pre-classified
       as an address-backed copy source. This composes the existing M86 result
       object seam with the M85 by-value argument seam; no aggregate SSA value
       or target ABI rule is introduced here. */
    if (expression->kind == MINIC_EXPRESSION_CALL &&
        expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
        return minic_core_lower_direct_record_call_object(context, expression, object_id);
    }
    status = minic_core_lower_internal_record_materialized_address(context, expression_id, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (!minic_core_function_add_object(context->source, expression->span, parameter_type, object_id) ||
        !minic_type_pointer_to(parameter_type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = expression->span;
    instruction_source_type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = *object_id;
    if (!minic_core_function_append_source_value_instruction(
            context->source, context->block_id, &instruction, instruction_source_type, &destination_address)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    instruction.span = expression->span;
    instruction_source_type = parameter_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.record_copy.destination_address = destination_address;
    instruction.value.record_copy.source_address = source_address;
    return minic_core_function_append_source_effect_instruction(
               context->source, context->block_id, &instruction, instruction_source_type)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}



/* The source CALL retains its canonical FunctionId even when several
 * specialized Core functions execute the same source FunctionBody. Resolve
 * its emitted callee once under this exact Core caller, after all source
 * lvalue mutations and CFG facts have been accounted for. */
typedef struct CoreInlineCallFact {
    bool integer_known;
    uint64_t integer_bits;
    bool symbolic_known;
    const char *symbol;
    int64_t symbol_addend;
    bool boolean_range_known;
} CoreInlineCallFact;

static bool core_inline_symbolic_fact_matches(
    const MinicCoreLowerContext *context,
    MinicExpressionId candidate_expression,
    const CoreInlineCallFact *actual) {
    const char *candidate_symbol = NULL;
    int64_t candidate_addend = 0;
    return actual != NULL && actual->symbolic_known &&
        actual->symbol != NULL &&
        core_inline_asm_symbolic_address_fact(
            context, candidate_expression, &candidate_symbol, &candidate_addend) &&
        candidate_symbol != NULL &&
        candidate_addend == actual->symbol_addend &&
        strcmp(candidate_symbol, actual->symbol) == 0;
}

/* Candidate ranking preserves the old Integer -> Symbolic -> Boolean
 * priority, but does not carry a partially validated clone between passes.
 * Every claimed fact must be proved at this callsite; a zero-fact section
 * clone remains a valid deliberate choice when the generic source may not
 * be machine-admissible. */
static const MinicFunction *core_select_direct_call_specialization(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    const MinicFunction *source_callee) {
    const MinicC0Program *program;
    MinicFunctionId source_id;
    const MinicFunction *source;
    const MinicFunction *best;
    CoreInlineCallFact actual[MINIC_MAX_FUNCTION_PARAMETERS];
    size_t best_integer = 0U, best_symbolic = 0U, best_boolean = 0U;
    size_t best_exact_symbolic = 0U;
    size_t candidate_index, i;

    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL ||
        expression == NULL || expression->kind != MINIC_EXPRESSION_CALL ||
        source_callee == NULL)
        return source_callee;
    source_id = expression->value.call.function_id;
    if (source_id >= program->function_count) return source_callee;
    if (source_callee->is_integer_specialization)
        source_id = source_callee->specialization_source;
    if (source_id >= program->function_count) return source_callee;
    source = &program->functions[source_id];
    if (!source->is_defined || !source->is_internal ||
        source->is_variadic || source->alias_target != MINIC_FUNCTION_INVALID ||
        source->parameter_count != expression->value.call.argument_count ||
        source->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS)
        return source_callee;

    (void)memset(actual, 0, sizeof(actual));
    for (i = 0U; i < source->parameter_count; ++i) {
        MinicExpressionId argument_id = minic_c0_call_argument(expression, i);
        MinicType formal = source->parameter_types[i];
        if (minic_type_is_integer(formal)) {
            MinicConstValue value, converted;
            if (core_const_eval_integer_with_locals(
                    context, argument_id, &value) &&
                minic_const_value_convert_integer(
                    program, context->target, &value, formal, &converted)) {
                actual[i].integer_known = true;
                actual[i].integer_bits = converted.bits;
            }
            actual[i].boolean_range_known =
                minic_core_lower_boolean_range_expression(
                    context, argument_id, 0U);
        } else if (minic_type_is_pointer(formal)) {
            actual[i].symbolic_known =
                core_inline_asm_symbolic_address_fact(
                    context, argument_id, &actual[i].symbol,
                    &actual[i].symbol_addend);
        }
    }

    best = source;
    for (candidate_index = 0U; candidate_index < program->function_count;
         ++candidate_index) {
        const MinicFunction *candidate = &program->functions[candidate_index];
        size_t integers = 0U, symbols = 0U, booleans = 0U;
        size_t exact_symbolic = 0U;
        bool compatible = true;
        if (!candidate->is_integer_specialization ||
            candidate->specialization_source != source_id ||
            candidate->parameter_count != source->parameter_count ||
            !minic_inline_spec_section_matches(
                candidate, source, context->source_function))
            continue;
        for (i = 0U; i < source->parameter_count; ++i) {
            if (candidate->specialization_integer_known[i]) {
                if (!actual[i].integer_known ||
                    actual[i].integer_bits !=
                        candidate->specialization_integer_bits[i]) {
                    compatible = false;
                    break;
                }
                ++integers;
            }
            if (candidate->specialization_symbolic_known[i]) {
                if (!core_inline_symbolic_fact_matches(
                        context, candidate->specialization_symbolic_expression[i],
                        &actual[i])) {
                    compatible = false;
                    break;
                }
                ++symbols;
                /* Two distinct volatile-inline callsites can have equal
                 * canonical address facts but still own separate preplanned
                 * clone bodies. Prefer the caller's exact symbolic source
                 * expression when available, without weakening the byte-
                 * addend proof required for every selected candidate. */
                if (candidate->specialization_symbolic_expression[i] ==
                    minic_c0_call_argument(expression, i))
                    ++exact_symbolic;
            }
            if (candidate->specialization_boolean_range_known[i]) {
                if (!actual[i].boolean_range_known) {
                    compatible = false;
                    break;
                }
                ++booleans;
            }
        }
        if (!compatible) continue;
        if (best == source ||
            integers > best_integer ||
            (integers == best_integer && symbols > best_symbolic) ||
            (integers == best_integer && symbols == best_symbolic &&
             booleans > best_boolean) ||
            (integers == best_integer && symbols == best_symbolic &&
             booleans == best_boolean && exact_symbolic > best_exact_symbolic)) {
            best = candidate;
            best_integer = integers;
            best_symbolic = symbols;
            best_boolean = booleans;
            best_exact_symbolic = exact_symbolic;
        }
    }
    if (best != source_callee) {
        const char *caller_name = context->source_function != NULL &&
            context->source_function->name != NULL
                ? context->source_function->name : "?";
        const char *from_name = source_callee->name != NULL
                                   ? source_callee->name : "?";
        const char *to_name = best->name != NULL ? best->name : "?";
        if (getenv("MINIC_INLINE_SPEC_TRANSITIVE_TRACE") != NULL)
            (void)fprintf(stderr,
                "INLINE_SPEC_TRANSITIVE_SELECT caller=%s from=%s to=%s known=%zu\n",
                caller_name, from_name, to_name, best_integer);
        if (getenv("MINIC_INLINE_SPEC_SYMBOLIC_TRACE") != NULL)
            (void)fprintf(stderr,
                "INLINE_SPEC_SYMBOLIC_SELECT caller=%s from=%s to=%s symbols=%zu\n",
                caller_name, from_name, to_name, best_symbolic);
        if (getenv("MINIC_INLINE_SPEC_BOOLEAN_TRACE") != NULL)
            (void)fprintf(stderr,
                "INLINE_SPEC_BOOLEAN_SELECT caller=%s from=%s to=%s ranges=%zu\n",
                caller_name, from_name, to_name, best_boolean);
    }
    return best;
}

/* One source evaluation and one typed VALUE/OBJECT snapshot per argument.
 * Direct, indirect and aggregate-returning calls must share this ordering.
 * The optional direct name preserves existing diagnostics for failed scalar
 * expression lowering; no ABI register assignment happens at this stage. */
static MinicCoreLowerStatus core_prepare_call_argument(
    MinicCoreLowerContext *context, const MinicExpression *call,
    size_t index, MinicType type, bool fixed, const char *direct_name,
    MinicCoreCallArgument *argument, MinicCoreObjectId *snapshot_object) {
    MinicCoreLowerStatus status;
    MinicExpressionId source;
    if (context == NULL || call == NULL || argument == NULL ||
        snapshot_object == NULL) return MINIC_CORE_LOWER_ERROR;
    source = minic_c0_call_argument(call, index);
    if (minic_type_is_record(type)) {
        argument->kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
        return lower_record_call_argument_object(
            context, source, type, &argument->value.object_id);
    }
    argument->kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    status = fixed
        ? minic_core_lower_internal_lower_scalar_assignment_value(
              context, type, source, &argument->value.value_id)
        : lower_expression(context, source, &argument->value.value_id);
    if (status != MINIC_CORE_LOWER_OK) {
        if (direct_name != NULL) {
            (void)fprintf(stderr,
                "CORE_LOWER_DETAIL marker=BATCH_D_VARIADIC_DIRECT_CALL function=%s "
                "stage=direct-call callee=%s arg=%zu fixed=%d reason=argument-lower status=%d\n",
                context->source_function != NULL
                    ? context->source_function->name : "?",
                direct_name, index, fixed ? 1 : 0, (int)status);
        }
        return status;
    }
    if (argument->value.value_id >= context->function->value_count ||
        !minic_type_equal(
            minic_core_function_source_value_type(context->source, argument->value.value_id), type))
        return MINIC_CORE_LOWER_ERROR;
    /* Preserve one immutable language fact before the ordinary Core
     * evaluation-order snapshot. Do not let backends infer immediates from
     * arbitrary LOAD/STORE aliases or skip call side-effect ordering.
     * No target ISA or CSR identity belongs in this generic call path. */
    argument->has_constant_integer = false;
    argument->constant_integer_bits = 0U;
    if (context->target != NULL && minic_type_is_integer(type)) {
        MinicConstValue integer;
        if (minic_const_eval_arithmetic_to_integer(
                context->body->program, context->target, source,
                type, &integer) &&
            minic_type_equal(integer.type, type)) {
            argument->has_constant_integer = true;
            argument->constant_integer_bits = integer.bits;
        }
    }
    return minic_core_lower_internal_spill_scalar_value(
        context, call->span, type, argument->value.value_id,
        snapshot_object);
}

/* Evaluate each source expression exactly once before replaying saved SSA
 * values in the eventual call block. An indirect callee may itself branch,
 * so the caller determines *when* this reload occurs. */
static MinicCoreLowerStatus core_reload_call_arguments(
    MinicCoreLowerContext *context, const MinicExpression *call,
    const MinicType *types, const MinicCoreObjectId *snapshots,
    MinicCoreCallArgument *arguments, size_t count) {
    size_t i;
    for (i = 0U; i < count; ++i) {
        MinicCoreLowerStatus status;
        if (arguments[i].kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) continue;
        status = minic_core_lower_internal_reload_scalar_value(
            context, call->span, types[i], snapshots[i],
            &arguments[i].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) return status;
    }
    return MINIC_CORE_LOWER_OK;
}

/* Direct scalar, void, and record calls share their one callee resolution,
 * source-order argument snapshot, Core callee registration, and CALL append
 * owner. Indirect calls retain their distinct first-class callee path below. */
static MinicCoreLowerStatus core_lower_direct_call_impl(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, MinicCoreObjectId *result_object) {
    const MinicFunction *callee;
    const char *callee_name;
    size_t callee_name_length;
    MinicCoreCalleeId callee_id;
    MinicCoreInstruction instruction;
    MinicType instruction_source_type = minic_type_void();
    MinicCoreCallArgument *arguments;
    MinicCoreObjectId argument_objects[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicType argument_types[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicCoreLowerStatus status;
    size_t argument_begin;
    size_t argument_count;
    size_t argument_index;
    bool returns_void;
    bool returns_record;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL ||
        ((value_id == NULL) == (result_object == NULL)) ||
        expression->kind != MINIC_EXPRESSION_CALL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value.call.function_id == MINIC_FUNCTION_INVALID)
        return result_object != NULL ? MINIC_CORE_LOWER_ERROR : MINIC_CORE_LOWER_UNSUPPORTED;

    callee = minic_c0_program_function(
        context->body->program, expression->value.call.function_id);
    callee = core_select_direct_call_specialization(context, expression, callee);
    if (callee == NULL || callee->name == NULL || callee->name_length == 0U)
        return result_object != NULL ? MINIC_CORE_LOWER_UNSUPPORTED : MINIC_CORE_LOWER_ERROR;
    callee_name = callee->assembler_name != NULL ? callee->assembler_name : callee->name;
    callee_name_length =
        callee->assembler_name != NULL ? callee->assembler_name_length : callee->name_length;
    if (callee_name == NULL || callee_name_length == 0U)
        return MINIC_CORE_LOWER_ERROR;

    argument_count = expression->value.call.argument_count;
    returns_void = minic_type_is_void(callee->return_type);
    returns_record = minic_type_is_record(callee->return_type);
    if (argument_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (!callee->is_variadic && argument_count != callee->parameter_count) ||
        (callee->is_variadic && argument_count < callee->parameter_count) ||
        (result_object != NULL
            ? (!minic_type_is_record(expression->type) || !returns_record ||
               !minic_type_equal(callee->return_type, expression->type) ||
               callee->is_variadic)
            : (returns_record || (!returns_void &&
               !core_memory_scalar_type(callee->return_type))))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    /* CONFIG-off internal void helpers with no effects can be elided only
     * when every original call argument is independently pure. Record
     * materialization is deliberately never elided. */
    if (value_id != NULL && returns_void && callee->is_defined &&
        callee->is_internal && callee->body_block != MINIC_BLOCK_INVALID) {
        const MinicBlock *callee_body = minic_c0_program_block(
            context->body->program, callee->body_block);
        bool empty_effect = callee_body != NULL && callee_body->statement_count == 0U;
        bool arguments_pure = true;

        if (callee_body != NULL && callee_body->statement_count == 1U) {
            const MinicStatement *first = minic_c0_program_statement(
                context->body->program, callee_body->statements[0]);
            empty_effect = first != NULL && first->kind == MINIC_STATEMENT_RETURN &&
                           first->expression == MINIC_EXPRESSION_INVALID &&
                           first->cleanup_context == first->cleanup_stop_context;
        }
        if (empty_effect) {
            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                if (!minic_core_cfg_pure_call_argument(
                        context, minic_c0_call_argument(expression, argument_index), 0U)) {
                    arguments_pure = false;
                    break;
                }
            }
            if (arguments_pure) {
                *value_id = MINIC_CORE_VALUE_INVALID;
                return MINIC_CORE_LOWER_OK;
            }
        }
    }

    for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
        if (argument_index < callee->parameter_count) {
            argument_types[argument_index] = callee->parameter_types[argument_index];
            if (!core_memory_scalar_type(argument_types[argument_index]) &&
                !minic_type_is_record(argument_types[argument_index]))
                return MINIC_CORE_LOWER_UNSUPPORTED;
        } else {
            const MinicExpression *argument_expression = minic_c0_program_expression(
                context->body->program, minic_c0_call_argument(expression, argument_index));
            if (argument_expression == NULL)
                return MINIC_CORE_LOWER_UNSUPPORTED;
            if (minic_type_is_record(argument_expression->type)) {
                if (!minic_type_unqualified(
                        argument_expression->type, &argument_types[argument_index]) ||
                    !minic_type_is_record(argument_types[argument_index]))
                    return MINIC_CORE_LOWER_UNSUPPORTED;
            } else if (!core_scalar_expression_value_type(
                           context->body, argument_expression, &argument_types[argument_index]) ||
                       !core_memory_scalar_type(argument_types[argument_index]))
                return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    }

    arguments = argument_count == 0U
                    ? NULL
                    : (MinicCoreCallArgument *)calloc(argument_count, sizeof(*arguments));
    if (argument_count != 0U && arguments == NULL)
        return MINIC_CORE_LOWER_ERROR;
    for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
        status = core_prepare_call_argument(
            context, expression, argument_index, argument_types[argument_index],
            argument_index < callee->parameter_count,
            result_object != NULL ? NULL : callee_name,
            &arguments[argument_index], &argument_objects[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    status = core_reload_call_arguments(
        context, expression, argument_types, argument_objects,
        arguments, argument_count);
    if (status != MINIC_CORE_LOWER_OK) {
        free(arguments);
        return status;
    }
    if (!minic_core_function_add_callee(context->source,
                                        callee_name,
                                        callee_name_length,
                                        callee->return_type,
                                        callee->parameter_types,
                                        callee->parameter_count,
                                        callee->is_variadic,
                                        callee->calling_convention,
                                        &callee_id) ||
        !minic_core_function_append_call_arguments(
            context->function, arguments, argument_count, &argument_begin)) {
        free(arguments);
        return MINIC_CORE_LOWER_ERROR;
    }
    free(arguments);

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_CALL;
    instruction.span = expression->span;
    instruction_source_type = callee->return_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.call.callee_id = callee_id;
    instruction.value.call.argument_begin = argument_begin;
    instruction.value.call.argument_count = argument_count;
    instruction.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (result_object != NULL) {
        if (!minic_core_function_add_object(
                context->source, expression->span, callee->return_type, result_object))
            return MINIC_CORE_LOWER_ERROR;
        instruction.value.call.result_object = *result_object;
        return minic_core_function_append_source_effect_instruction(
                   context->source, context->block_id, &instruction, instruction_source_type)
                   ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    }
    if (returns_void) {
        *value_id = MINIC_CORE_VALUE_INVALID;
        return minic_core_function_append_source_effect_instruction(
                   context->source, context->block_id, &instruction, instruction_source_type)
                   ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    }
    return minic_core_function_append_source_value_instruction(
               context->source, context->block_id, &instruction, instruction_source_type, value_id)
               ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_direct_call(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id) {
    return core_lower_direct_call_impl(context, expression, value_id, NULL);
}

MinicCoreLowerStatus minic_core_lower_direct_record_call_object(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreObjectId *result_object) {
    return core_lower_direct_call_impl(context, expression, NULL, result_object);
}

/* M83_FIRST_CLASS_INDIRECT_CALL: keep the callee as a first-class SSA
   function-pointer value; the static signature is copied into Core so
   verification and later backends do not depend on frontend Program state.
   The return destination follows one target-neutral dual form for *all*
   calling conventions: scalar SSA or address-backed aggregate object. */
static MinicCoreLowerStatus core_lower_indirect_call_impl(MinicCoreLowerContext *context,
                                                const MinicExpression *expression,
                                                MinicCoreValueId *value_id,
                                                MinicCoreObjectId *result_object) {
    const MinicExpression *callee_expression;
    const MinicFunctionType *signature;
    MinicCoreCallSignatureId signature_id;
    MinicCoreInstruction instruction;
    MinicType instruction_source_type = minic_type_void();
    MinicCoreValueId callee_value;
    MinicCoreCallArgument *arguments;
    MinicCoreObjectId argument_objects[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicType argument_types[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicCoreLowerStatus status;
    MinicExpressionId callee_value_expression_id;
    MinicType callee_value_type;
    MinicType function_type;
    size_t argument_begin;
    size_t argument_count;
    size_t argument_index;
    bool returns_void;
    bool returns_record;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL ||
        ((value_id == NULL) == (result_object == NULL)) ||
        expression->kind != MINIC_EXPRESSION_CALL ||
        expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    callee_value_expression_id = expression->value.call.callee;
    callee_expression =
        minic_c0_program_expression(context->body->program, callee_value_expression_id);
    if (callee_expression != NULL &&
        callee_expression->kind == MINIC_EXPRESSION_DEREFERENCE &&
        minic_type_is_function(callee_expression->type)) {
        const MinicExpression *pointer_operand;

        callee_value_expression_id = callee_expression->value.unary.operand;
        pointer_operand = minic_c0_program_expression(
            context->body->program, callee_value_expression_id);
        if (pointer_operand == NULL ||
            !core_scalar_expression_value_type(
                context->body, pointer_operand, &callee_value_type) ||
            !minic_type_pointee(callee_value_type, &function_type) ||
            !minic_type_is_function(function_type) ||
            !minic_type_equal(function_type, callee_expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    } else if (callee_expression == NULL ||
               !core_scalar_expression_value_type(
                   context->body, callee_expression, &callee_value_type) ||
               !minic_type_pointee(callee_value_type, &function_type) ||
               !minic_type_is_function(function_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    signature = minic_c0_program_function_type(
        context->body->program, function_type.function_type_id);
    argument_count = expression->value.call.argument_count;
    if (signature == NULL || argument_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (!signature->is_variadic && argument_count != signature->parameter_count) ||
        (signature->is_variadic && argument_count < signature->parameter_count) ||
        !minic_type_equal(expression->type, signature->return_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    returns_void = minic_type_is_void(signature->return_type);
    returns_record = minic_type_is_record(signature->return_type);
    if (result_object != NULL ? !returns_record :
        (returns_record || (!returns_void &&
         !core_memory_scalar_type(signature->return_type)))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    arguments = argument_count == 0U
                    ? NULL
                    : (MinicCoreCallArgument *)calloc(argument_count, sizeof(*arguments));
    if (argument_count != 0U && arguments == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    /* M151_INDIRECT_CALL_BATCH_OWNER: fixed arguments use the same scalar or
       address-backed record transport as direct calls. A variadic tail keeps
       the actual scalar type. Every VALUE is spilled until the callee has been
       evaluated so the final indirect call block owns all SSA inputs. */
    for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
        bool fixed = argument_index < signature->parameter_count;
        if (fixed) {
            argument_types[argument_index] = signature->parameter_types[argument_index];
            if (!minic_type_is_record(argument_types[argument_index]) &&
                !core_memory_scalar_type(argument_types[argument_index])) {
                free(arguments);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        } else {
            const MinicExpression *arg = minic_c0_program_expression(
                context->body->program,
                minic_c0_call_argument(expression, argument_index));
            if (arg == NULL ||
                !core_scalar_expression_value_type(
                    context->body, arg, &argument_types[argument_index]) ||
                !core_memory_scalar_type(argument_types[argument_index])) {
                free(arguments);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        }
        status = core_prepare_call_argument(
            context, expression, argument_index,
            argument_types[argument_index], fixed, NULL,
            &arguments[argument_index], &argument_objects[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }

    status = lower_expression(context, callee_value_expression_id, &callee_value);
    if (status != MINIC_CORE_LOWER_OK) {
        free(arguments);
        return status;
    }
    status = core_reload_call_arguments(
        context, expression, argument_types, argument_objects,
        arguments, argument_count);
    if (status != MINIC_CORE_LOWER_OK) {
        free(arguments);
        return status;
    }
    if (callee_value >= context->function->value_count ||
        !minic_type_equal(minic_core_function_source_value_type(context->source, callee_value), callee_value_type)) {
        free(arguments);
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_core_function_add_call_signature(context->source,
                                                function_type.function_type_id,
                                                signature->return_type,
                                                signature->parameter_types,
                                                signature->parameter_count,
                                                signature->is_variadic,
                                                signature->calling_convention,
                                                &signature_id) ||
        !minic_core_function_append_call_arguments(
            context->function, arguments, argument_count, &argument_begin)) {
        free(arguments);
        return MINIC_CORE_LOWER_ERROR;
    }
    free(arguments);

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    instruction.span = expression->span;
    instruction_source_type = signature->return_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.indirect_call.callee = callee_value;
    instruction.value.indirect_call.signature_id = signature_id;
    instruction.value.indirect_call.argument_begin = argument_begin;
    instruction.value.indirect_call.argument_count = argument_count;
    instruction.value.indirect_call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (returns_record) {
        if (!minic_core_function_add_object(
                context->source, expression->span,
                signature->return_type, result_object)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.value.indirect_call.result_object = *result_object;
        return minic_core_function_append_source_effect_instruction(
                   context->source, context->block_id, &instruction, instruction_source_type)
                   ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    }
    if (returns_void) {
        *value_id = MINIC_CORE_VALUE_INVALID;
        return minic_core_function_append_source_effect_instruction(
                   context->source, context->block_id, &instruction, instruction_source_type)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    return minic_core_function_append_source_value_instruction(
               context->source, context->block_id, &instruction, instruction_source_type, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_indirect_call(MinicCoreLowerContext *context,
                                                const MinicExpression *expression,
                                                MinicCoreValueId *value_id) {
    return core_lower_indirect_call_impl(context, expression, value_id, NULL);
}

MinicCoreLowerStatus minic_core_lower_indirect_record_call_object(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreObjectId *result_object) {
    return core_lower_indirect_call_impl(context, expression, NULL, result_object);
}

