#include "core/core_lower_internal.h"
#include "core/source_loop_shapes.h"
#include "core/local_facts.h"
#include "core/cfg_constant_calls.h"

#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Core lowering consumes the selected target's read-only DataLayout.
 * Never bypass TargetInfo through the process-wide default target: that would
 * make the AST -> Core seam implicitly RV64-specific. */

MinicCoreLowerStatus minic_core_lower_internal_lower_expression_statement(
    MinicCoreLowerContext *context, const MinicStatement *statement);
MinicCoreLowerStatus
minic_core_lower_internal_lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated);
MinicCoreLowerStatus minic_core_lower_internal_set_branch(MinicCoreLowerContext *context,
                                       MinicCoreBlockId block_id,
                                       MinicSourceSpan span,
                                       MinicCoreBlockId target);
void core_local_constant_set(MinicCoreLowerContext *context,
                                    MinicLocalId local_id,
                                    const MinicConstValue *value);
bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,
                                                MinicExpressionId expression_id,
                                                MinicConstValue *value);



MinicCoreLowerStatus minic_core_lower_internal_lower_condition_branch(MinicCoreLowerContext *context,
                                                   MinicExpressionId expression_id,
                                                   MinicSourceSpan span,
                                                   MinicCoreBlockId when_true,
                                                   MinicCoreBlockId when_false);

MinicCoreLowerStatus minic_core_lower_internal_spill_scalar_value(MinicCoreLowerContext *context,
                                               MinicSourceSpan span,
                                               MinicType type,
                                               MinicCoreValueId value_id,
                                               MinicCoreObjectId *object_id);
MinicCoreLowerStatus minic_core_lower_internal_reload_scalar_value(MinicCoreLowerContext *context,
                                                MinicSourceSpan span,
                                                MinicType type,
                                                MinicCoreObjectId object_id,
                                                MinicCoreValueId *value_id);
MinicCoreLowerStatus minic_core_lower_internal_append_integer_conversion(MinicCoreLowerContext *context,
                                                      MinicSourceSpan span,
                                                      MinicType target_type,
                                                      MinicCoreValueId source_value,
                                                      MinicCoreValueId *value_id);
MinicCoreLowerStatus minic_core_lower_internal_lower_scalar_assignment_value(MinicCoreLowerContext *context,
                                                          MinicType target_type,
                                                          MinicExpressionId expression_id,
                                                          MinicCoreValueId *value_id);


MinicCoreLowerStatus ensure_statement_block(MinicCoreLowerContext *context, MinicStatementId statement_id, MinicCoreBlockId *block_id) {
    const MinicStatement *source_statement;
    MinicCoreBlockId mapped;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || block_id == NULL || context->statement_blocks == NULL ||
        statement_id >= context->statement_block_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    mapped = context->statement_blocks[statement_id];
    if (mapped == MINIC_CORE_BLOCK_INVALID) {
        if (!minic_core_function_add_block(context->function, &mapped)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        context->statement_blocks[statement_id] = mapped;
    }
    if (mapped >= context->function->block_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source_statement = minic_c0_program_statement(context->body->program, statement_id);
    if (source_statement == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (source_statement->kind == MINIC_STATEMENT_LABEL) {
        MinicCoreBlock *block = &context->function->blocks[mapped];

        if (block->source_label_id != SIZE_MAX &&
            block->source_label_id != (size_t)statement_id) {
            return MINIC_CORE_LOWER_ERROR;
        }
        block->source_label_id = (size_t)statement_id;
    }
    *block_id = mapped;
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_internal_local_object(MinicCoreLowerContext *context,
                                               MinicLocalId local_id,
                                               MinicCoreObjectId *object_id) {
    const MinicLocal *local;
    size_t local_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->source_function == NULL || context->function == NULL || object_id == NULL ||
        local_id < context->source_function->local_begin) {
        return MINIC_CORE_LOWER_ERROR;
    }
    local_index = local_id - context->source_function->local_begin;
    if (local_index >= context->source_function->local_count || context->local_objects == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (context->local_objects[local_index] != MINIC_CORE_OBJECT_INVALID) {
        *object_id = context->local_objects[local_index];
        return MINIC_CORE_LOWER_OK;
    }
    local = minic_c0_program_local(context->body->program, local_id);
    if (local == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    /* M105_FIXED_REGISTER_STRUCTURED_ASM: a GNU local register binding does
       not change the C object's scalar value semantics. Keep ordinary Core
       storage for reads/writes; only inline-asm operand materialization consumes
       the target register binding. */
    /* M106_MATERIALIZED_LOCAL_ARRAY_OBJECT: frontend array convergence has
       two local-object forms. Legacy locals keep element type + is_array/count;
       typedef/materialized locals carry one complete array MinicType directly.
       A materialized array is one Core object whose DataLayout already owns the
       full extent, so its address is naturally pointer-to-array. */
    /* M175A_REPEATED_ARRAY_OBJECT: an outer legacy array may itself have a
       materialized array element type (for example `typedef int Row[3];
       Row rows[2];`).  In that mixed representation local->type describes one
       complete element object and local->element_count describes the outer
       repetition.  Preserve both dimensions by using Core's repeated-object
       form instead of rejecting local->is_array. */
    if (minic_type_is_array(local->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(
            context->body->program, local->type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U ||
            array_type->is_zero_length) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (local->is_array) {
            if (local->element_count == 0U ||
                !minic_core_function_add_repeated_object(context->function,
                                                         local->name_span,
                                                         local->type,
                                                         local->element_count,
                                                         object_id)) {
                return MINIC_CORE_LOWER_ERROR;
            }
        } else if (!minic_core_function_add_object(
                       context->function, local->name_span, local->type, object_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        context->function->objects[*object_id].explicit_alignment = local->explicit_alignment;
        context->local_objects[local_index] = *object_id;
        return MINIC_CORE_LOWER_OK;
    }
    if (!core_memory_scalar_type(local->type) && !minic_type_is_record(local->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (local->is_array) {
        /* A zero-length GNU local still has an address.  Allocate one
           addressable backing element as a sentinel; its source-language
           extent and sizeof remain zero, and no element access is valid. */
        if ((local->element_count == 0U && !local->is_zero_length_array) ||
            !minic_core_function_add_repeated_object(context->function,
                                                    local->name_span,
                                                    local->type,
                                                    local->element_count == 0U ? 1U
                                                                               : local->element_count,
                                                    object_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    } else if (!minic_core_function_add_object(
                   context->function, local->name_span, local->type, object_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    context->function->objects[*object_id].explicit_alignment = local->explicit_alignment;
    context->local_objects[local_index] = *object_id;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_parameter_ingress(MinicCoreLowerContext *context) {
    size_t parameter_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->source_function == NULL || context->function == NULL ||
        context->source_function->parameter_count > context->source_function->local_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (parameter_index = 0U; parameter_index < context->source_function->parameter_count;
         ++parameter_index) {
        const MinicLocal *parameter;
        MinicCoreInstruction instruction;
        MinicCoreObjectId object_id;
        MinicCoreLowerStatus status;
        MinicLocalId local_id;
        MinicType parameter_value_type;

        local_id = context->source_function->local_begin + parameter_index;
        parameter = minic_c0_program_local(context->body->program, local_id);
        if (parameter == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (parameter->is_array || parameter->is_register_storage ||
            !minic_type_unqualified(parameter->type, &parameter_value_type) ||
            !minic_c0_types_compatible(
                context->body->program, parameter_value_type,
                context->source_function->parameter_types[parameter_index])) {
            (void)fprintf(stderr,
                          "CORE_M158_INGRESS_DETAIL function=%s parameter=%zu "
                          "volatile=%d array=%d register=%d\n",
                          context->source_function->name,
                          parameter_index,
                          minic_type_is_volatile(parameter->type) ? 1 : 0,
                          parameter->is_array ? 1 : 0,
                          parameter->is_register_storage ? 1 : 0);
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = minic_core_lower_internal_local_object(context, local_id, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr,
                          "CORE_M158_INGRESS_DETAIL function=%s parameter=%zu local_object_status=%d\n",
                          context->source_function->name,
                          parameter_index,
                          (int)status);
            return status;
        }

        if (minic_type_is_record(parameter_value_type)) {
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT;
            instruction.span = parameter->name_span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.parameter_object.parameter_index = parameter_index;
            instruction.value.parameter_object.object_id = object_id;
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &instruction)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            continue;
        }
        if (!core_memory_scalar_type(parameter_value_type)) {
            (void)fprintf(stderr,
                          "CORE_M158_INGRESS_DETAIL function=%s parameter=%zu nonscalar=1\n",
                          context->source_function->name,
                          parameter_index);
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        {
            MinicCoreValueId address_id;
            MinicCoreValueId parameter_value;
            MinicType pointer_type;

            (void)memset(&instruction, 0, sizeof(instruction));
            if (context->source_function->is_integer_specialization &&
                context->source_function->specialization_integer_known[parameter_index] &&
                minic_type_is_integer(parameter_value_type)) {
                uint64_t specialized_bits =
                    context->source_function->specialization_integer_bits[parameter_index];
                instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                (void)memcpy(&instruction.value.integer_value,
                             &specialized_bits,
                             sizeof(specialized_bits));
            } else {
                instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
                instruction.value.parameter_index = parameter_index;
            }
            instruction.span = parameter->name_span;
            instruction.type = parameter_value_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &parameter_value)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (context->source_function->is_integer_specialization &&
                context->source_function->specialization_integer_known[parameter_index] &&
                minic_type_is_integer(parameter_value_type)) {
                MinicConstValue specialized_value;
                specialized_value.type = parameter_value_type;
                specialized_value.bits =
                    context->source_function->specialization_integer_bits[parameter_index];
                core_local_constant_set(context, local_id, &specialized_value);
                {
                    size_t specialized_index;
                    if (minic_core_local_fact_index(context, local_id, &specialized_index) &&
                        context->local_integer_constants[specialized_index].known) {
                        MinicCoreLocalIntegerConstant *fact =
                            &context->local_integer_constants[specialized_index];
                        fact->specialization_value = fact->value;
                        fact->specialization_known = true;
                        fact->specialization_modified = false;
                        if (minic_core_inline_spec_fact_trace_enabled(context)) {
                            (void)fprintf(stderr,
                                          "INLINE_SPEC_FACT event=seed function=%s source=%zu local=%zu bits=%" PRIu64 "\n",
                                          context->source_function->name != NULL
                                              ? context->source_function->name
                                              : "?",
                                          (size_t)context->source_function->specialization_source,
                                          (size_t)local_id,
                                          fact->specialization_value.bits);
                        }
                    }
                }
                if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
                    (void)fprintf(stderr,
                                  "INLINE_SPEC_PARAMETER_FACT function=%s parameter=%zu local=%zu bits=%" PRIu64 "\\n",
                                  context->source_function->name != NULL
                                      ? context->source_function->name
                                      : "?",
                                  parameter_index,
                                  (size_t)local_id,
                                  specialized_value.bits);
                }
            }

            if (!minic_type_pointer_to(parameter->type, &pointer_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
            instruction.span = parameter->name_span;
            instruction.type = pointer_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.object_id = object_id;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &address_id)) {
                return MINIC_CORE_LOWER_ERROR;
            }

            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
            instruction.span = parameter->name_span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.store.address = address_id;
            instruction.value.store.stored_value = parameter_value;
            instruction.value.store.is_volatile = minic_type_is_volatile(parameter->type);
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &instruction)) {
                return MINIC_CORE_LOWER_ERROR;
            }
        }
    }
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_internal_append_field_address(MinicCoreLowerContext *context,
                                                 MinicSourceSpan span,
                                                 MinicCoreValueId base_id,
                                                 MinicRecordId record_id,
                                                 size_t field_index,
                                                 MinicType field_type,
                                                 MinicCoreValueId *address_id) {
    MinicCoreInstruction instruction;
    MinicType base_pointee;
    const MinicRecord *record;
    const MinicRecordField *field;
    size_t byte_offset;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || address_id == NULL ||
        base_id >= context->function->value_count ||
        !minic_type_pointee(context->function->values[base_id].type, &base_pointee) ||
        !minic_type_is_record(base_pointee) || base_pointee.record_id != record_id ||
        record_id == MINIC_RECORD_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    record = minic_c0_program_record(context->body->program, record_id);
    field = minic_c0_record_field(record, field_index);
    if (record == NULL || field == NULL ||
        !minic_data_layout_record_field_offset(core_data_layout(context),
                                               context->body->program,
                                               record,
                                               field_index,
                                               &byte_offset)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_FIELD_ADDRESS;
    instruction.span = span;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.field_address.base = base_id;
    instruction.value.field_address.record_id = record_id;
    instruction.value.field_address.field_index = field_index;
    instruction.value.field_address.byte_offset = byte_offset;
    if (!minic_type_pointer_to(field_type, &instruction.type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, address_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static bool core_is_materialized_cleanup_statement(
    const MinicCoreLowerContext *context, const MinicStatement *statement) {
    size_t cleanup_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        statement == NULL || statement->kind != MINIC_STATEMENT_EXPRESSION ||
        statement->expression == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    for (cleanup_index = 0U; cleanup_index < context->body->program->cleanup_context_count;
         ++cleanup_index) {
        if (context->body->program->cleanup_contexts[cleanup_index].cleanup_expression ==
            statement->expression) {
            return true;
        }
    }
    return false;
}

static MinicCoreLowerStatus lower_cleanup_contexts(
    MinicCoreLowerContext *context, MinicCleanupContextId current, MinicCleanupContextId stop) {
    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        !minic_c0_cleanup_context_reaches(context->body->program, current, stop)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    while (current != stop) {
        const MinicCleanupContext *cleanup;
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        cleanup = minic_c0_program_cleanup_context(context->body->program, current);
        if (cleanup == NULL || cleanup->parent == current ||
            cleanup->cleanup_expression == MINIC_EXPRESSION_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_expression(context, cleanup->cleanup_expression, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        current = cleanup->parent;
    }
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_return(MinicCoreLowerContext *context,
                                         const MinicStatement *statement) {
    MinicCoreTerminator terminator;
    MinicCoreLowerStatus status;
    MinicCoreObjectId cleanup_return_object;
    bool has_cleanup;

    if (context == NULL || context->source_function == NULL || statement == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    has_cleanup = statement->cleanup_context != statement->cleanup_stop_context;
    cleanup_return_object = MINIC_CORE_OBJECT_INVALID;
    if (has_cleanup && !minic_c0_cleanup_context_reaches(
            context->body->program, statement->cleanup_context, statement->cleanup_stop_context)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.span = statement->span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    if (minic_type_is_void(context->source_function->return_type)) {
        if (statement->expression != MINIC_EXPRESSION_INVALID) {
            const MinicExpression *return_expression;
            MinicCoreValueId discarded_value;

            return_expression = minic_c0_program_expression(
                context->body->program, statement->expression);
            if (return_expression == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            /* GNU permits a void function to return any expression whose
               semantic type is void. The expression layer already owns the
               individual effect forms (calls, discard casts, statement
               expressions, conditionals, inline-asm wrappers). RETURN should
               sequence that semantic owner rather than re-whitelist source
               spellings. */
            if (!minic_type_is_void(return_expression->type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_expression(context, statement->expression, &discarded_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (discarded_value != MINIC_CORE_VALUE_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (context->block_id >= context->function->block_count ||
                context->function->blocks[context->block_id].has_terminator) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        }
    } else {
        if (statement->expression == MINIC_EXPRESSION_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (core_memory_scalar_type(context->source_function->return_type)) {
            /* M175B_SCALAR_RETURN_OWNER: all Core memory scalars use the same
               C assignment/value transport at the return boundary. */
            status = minic_core_lower_internal_lower_scalar_assignment_value(context,
                                                   context->source_function->return_type,
                                                   statement->expression,
                                                   &terminator.return_value);
        } else if (minic_type_is_record(context->source_function->return_type)) {
            const MinicExpression *expression;

            /* Aggregate return cleanup needs an address-backed return owner.
               Keep that capability fail-closed until Core models it explicitly. */
            if (has_cleanup) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            MinicType value_type;

            expression = minic_c0_program_expression(context->body->program, statement->expression);
            if (expression == NULL || !minic_type_is_record(expression->type) ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !minic_type_equal(value_type, context->source_function->return_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            if (expression->kind == MINIC_EXPRESSION_LOCAL &&
                expression->value_category == MINIC_VALUE_LVALUE) {
                const MinicLocal *local;

                local = minic_c0_program_local(context->body->program, expression->value.local_id);
                if (local == NULL || !minic_type_is_record(local->type)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                status = minic_core_lower_internal_local_object(
                    context, expression->value.local_id, &terminator.return_object);
            } else if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL &&
                       minic_c0_record_value_is_address_backed(
                           context->body->program, statement->expression)) {
                status = minic_core_lower_record_compound_literal_object(
                    context, expression, &terminator.return_object);
            } else if (expression->kind == MINIC_EXPRESSION_CALL &&
                       expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
                status = minic_core_lower_direct_record_call_object(
                    context, expression, &terminator.return_object);
            } else if (expression->kind == MINIC_EXPRESSION_CALL) {
                status = minic_core_lower_indirect_record_call_object(
                    context, expression, &terminator.return_object);
            } else if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
                       minic_type_is_record(expression->type)) {
                status = minic_core_lower_record_conditional_object(
                    context, expression, &terminator.return_object);
            } else if (expression->kind == MINIC_EXPRESSION_STATEMENT ||
                       expression->kind == MINIC_EXPRESSION_ASSIGNMENT ||
                       (expression->kind == MINIC_EXPRESSION_MEMBER &&
                        expression->value_category == MINIC_VALUE_RVALUE &&
                        minic_c0_record_value_is_copy_source(
                            context->body->program, statement->expression)) ||
                       minic_c0_record_value_is_address_backed(
                           context->body->program, statement->expression)) {
                status = minic_core_lower_record_load_object(
                    context, statement->expression, &terminator.return_object);
            } else {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        } else {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    /* A return expression is sequenced before GNU cleanup functions, while the
       computed scalar return value must survive those calls. Spill the value,
       execute exactly the cleanup contexts crossed by the return edge, then
       reload it before installing the RETURN terminator. */
    if (has_cleanup) {
        if (core_memory_scalar_type(context->source_function->return_type)) {
            status = minic_core_lower_internal_spill_scalar_value(context, statement->span,
                                        context->source_function->return_type,
                                        terminator.return_value, &cleanup_return_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        status = lower_cleanup_contexts(context, statement->cleanup_context,
                                        statement->cleanup_stop_context);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (core_memory_scalar_type(context->source_function->return_type)) {
            status = minic_core_lower_internal_reload_scalar_value(context, statement->span,
                                         context->source_function->return_type,
                                         cleanup_return_object, &terminator.return_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    }
    return minic_core_function_set_terminator(context->function, context->block_id, &terminator)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_internal_set_branch(MinicCoreLowerContext *context,
                                       MinicCoreBlockId block_id,
                                       MinicSourceSpan span,
                                       MinicCoreBlockId target) {
    MinicCoreTerminator terminator;

    if (context == NULL || context->function == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_BRANCH;
    terminator.span = span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.branch_target = target;
    return minic_core_function_set_terminator(context->function, block_id, &terminator)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_internal_lower_condition_branch(MinicCoreLowerContext *context,
                                                   MinicExpressionId expression_id,
                                                   MinicSourceSpan span,
                                                   MinicCoreBlockId when_true,
                                                   MinicCoreBlockId when_false) {
    const MinicExpression *expression;
    MinicCoreBlockId condition_block;
    MinicCoreTerminator terminator;
    MinicCoreValueId condition;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression_id == MINIC_EXPRESSION_INVALID ||
        when_true == MINIC_CORE_BLOCK_INVALID || when_false == MINIC_CORE_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL ||
        (!minic_type_is_integer(expression->type) && !minic_type_is_pointer(expression->type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {
        MinicConstValue operand_constant;
        bool operand_is_zero;
        bool fold_false = false;

        if (minic_const_eval_integer(context->body->program,
                                     context->target,
                                     expression->value.binary.left,
                                     &operand_constant) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &operand_constant,
                                      &operand_is_zero) &&
            operand_is_zero &&
            minic_core_cfg_pure_call_argument(
                context, expression->value.binary.right, 0U)) {
            fold_false = true;
        } else if (minic_const_eval_integer(context->body->program,
                                            context->target,
                                            expression->value.binary.right,
                                            &operand_constant) &&
                   minic_const_value_is_zero(context->body->program,
                                             context->target,
                                             &operand_constant,
                                             &operand_is_zero) &&
                   operand_is_zero &&
                   minic_core_cfg_pure_call_argument(
                       context, expression->value.binary.left, 0U)) {
            fold_false = true;
        }
        if (fold_false) {
            return minic_core_lower_internal_set_branch(context, context->block_id, span, when_false);
        }
    }
    if (minic_type_is_integer(expression->type)) {
        MinicConstValue condition_constant;
        bool condition_is_zero;
        if (core_const_eval_integer_with_locals(context, expression_id, &condition_constant) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &condition_constant,
                                      &condition_is_zero)) {
            return minic_core_lower_internal_set_branch(context,
                              context->block_id,
                              span,
                              condition_is_zero ? when_false : when_true);
        }
    }
    if (context->source_function != NULL &&
        context->source_function->name != NULL &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0) {
        (void)fprintf(stderr,
                      "GENL_COND_ENTER expression=%zu kind=%d block=%u true=%u false=%u op=%d\\n",
                      (size_t)expression_id,
                      (int)expression->kind,
                      (unsigned int)context->block_id,
                      (unsigned int)when_true,
                      (unsigned int)when_false,
                      expression->kind == MINIC_EXPRESSION_BINARY
                          ? (int)expression->value.binary.operator_kind
                          : expression->kind == MINIC_EXPRESSION_UNARY
                          ? (int)expression->value.unary.operator_kind : -1);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        const MinicExpression *operand;

        operand =
            minic_c0_program_expression(context->body->program, expression->value.unary.operand);
        if (operand != NULL &&
            (minic_type_is_integer(operand->type) || minic_type_is_pointer(operand->type))) {
            return minic_core_lower_internal_lower_condition_branch(
                context, expression->value.unary.operand, span, when_false, when_true);
        }
    }
    if (expression->kind == MINIC_EXPRESSION_CONVERSION && context->target != NULL) {
        const MinicExpression *operand;
        unsigned int source_width;
        unsigned int destination_width;

        operand =
            minic_c0_program_expression(context->body->program, expression->value.unary.operand);
        if (operand != NULL && minic_type_is_integer(operand->type) &&
            minic_type_is_integer(expression->type) &&
            minic_target_info_integer_width(
                context->target, context->body->program, operand->type, &source_width) &&
            minic_target_info_integer_width(
                context->target, context->body->program, expression->type, &destination_width) &&
            (minic_type_equal(operand->type, expression->type) ||
             destination_width > source_width)) {
            return minic_core_lower_internal_lower_condition_branch(
                context, expression->value.unary.operand, span, when_true, when_false);
        }
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR)) {
        const MinicExpression *left_expression;
        MinicCoreLocalIntegerConstant *skipped_rhs_facts;
        MinicCoreLocalIntegerConstant *evaluated_rhs_facts;
        MinicConstValue left_constant;
        bool is_and = expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND;
        bool left_is_zero;
        MinicCoreBlockId right_block;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        if (left_expression != NULL && minic_type_is_integer(left_expression->type) &&
            minic_const_eval_integer(context->body->program,
                                     context->target,
                                     expression->value.binary.left,
                                     &left_constant) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &left_constant,
                                      &left_is_zero)) {
            if (is_and && left_is_zero)
                return minic_core_lower_internal_set_branch(
                    context, context->block_id, span, when_false);
            if (!is_and && !left_is_zero)
                return minic_core_lower_internal_set_branch(
                    context, context->block_id, span, when_true);
            return minic_core_lower_internal_lower_condition_branch(context,
                expression->value.binary.right, span, when_true, when_false);
        }
        if (!minic_core_function_add_block(context->function, &right_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = minic_core_lower_internal_lower_condition_branch(
            context, expression->value.binary.left, span,
            is_and ? right_block : when_true,
            is_and ? when_false : right_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        /* An RHS assignment executes on only one edge. Its local constant
         * facts cannot be used after the logical expression unless the
         * skipped edge proves the same value. Source side effects while
         * evaluating the LHS have already occurred before this snapshot. */
        skipped_rhs_facts = minic_core_local_facts_snapshot(context);
        if (context->source_function != NULL &&
            context->source_function->local_count != 0U &&
            skipped_rhs_facts == NULL) return MINIC_CORE_LOWER_ERROR;
        context->block_id = right_block;
        status = minic_core_lower_internal_lower_condition_branch(
            context, expression->value.binary.right, span, when_true, when_false);
        if (status != MINIC_CORE_LOWER_OK) {
            free(skipped_rhs_facts);
            return status;
        }
        evaluated_rhs_facts = minic_core_local_facts_snapshot(context);
        if (context->source_function != NULL &&
            context->source_function->local_count != 0U &&
            evaluated_rhs_facts == NULL) {
            free(skipped_rhs_facts);
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_core_local_facts_meet(context, skipped_rhs_facts,
                                          evaluated_rhs_facts)) {
            free(evaluated_rhs_facts);
            free(skipped_rhs_facts);
            return MINIC_CORE_LOWER_ERROR;
        }
        free(evaluated_rhs_facts);
        free(skipped_rhs_facts);
        return MINIC_CORE_LOWER_OK;
    }

    if (context->source_function != NULL &&
        context->source_function->name != NULL &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0) {
        (void)fprintf(stderr,
                      "GENL_COND_EMIT expression=%zu kind=%d block=%u true=%u false=%u\n",
                      (size_t)expression_id,
                      (int)expression->kind,
                      (unsigned int)context->block_id,
                      (unsigned int)when_true,
                      (unsigned int)when_false);
    }
    status = lower_expression(context, expression_id, &condition);
    if (status != MINIC_CORE_LOWER_OK) {
        if (getenv("MINIC_CORE_EXPR_FAILURE_TRACE") != NULL)
            (void)fprintf(stderr,
                "CORE_EXPR_FAIL owner=condition function=%s status=%d "
                "expression=%zu kind=%d op=%d block=%u span=%zu:%zu\n",
                context->source_function != NULL ? context->source_function->name : "?",
                (int)status, (size_t)expression_id, (int)expression->kind,
                expression->kind == MINIC_EXPRESSION_BINARY
                    ? (int)expression->value.binary.operator_kind : -1,
                (unsigned)context->block_id,
                expression->span.begin.line, expression->span.begin.column);
        return status;
    }
    if (condition >= context->function->value_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (minic_type_is_pointer(expression->type)) {
        MinicCoreInstruction zero_test;
        MinicCoreBlockId original_true;

        if (!minic_type_is_pointer(context->function->values[condition].type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&zero_test, 0, sizeof(zero_test));
        zero_test.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        zero_test.span = span;
        zero_test.type = minic_type_int();
        zero_test.result = MINIC_CORE_VALUE_INVALID;
        zero_test.value.operand = condition;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &zero_test, &condition)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        original_true = when_true;
        when_true = when_false;
        when_false = original_true;
    } else if (!minic_type_is_integer(context->function->values[condition].type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    condition_block = context->block_id;
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    terminator.span = span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.conditional.condition = condition;
    terminator.conditional.when_true = when_true;
    terminator.conditional.when_false = when_false;
    return minic_core_function_set_terminator(context->function, condition_block, &terminator)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

/* M134_UNREACHABLE_TAIL_OWNER: once a structured path has a Core
   terminator, ordinary following statements have no runtime semantics and must
   not make strict lowering fail merely because their expression/control-flow
   owner is unsupported. A structured subtree may be pruned only when no jump
   originates outside that subtree and targets a label inside it. This matters
   because parser-normalized loops contain their own internal goto/label edges;
   those edges disappear together with the unreachable subtree and are not
   function-scope re-entry. Direct goto and asm-goto from outside remain
   fail-closed. Normalized blocks may form a graph, so membership discovery is
   cycle-safe. */
static bool core_mark_block_statement_membership(
    const MinicCoreLowerContext *context,
    MinicBlockId block_id,
    bool *visited_blocks,
    size_t block_count,
    bool *statement_membership,
    size_t statement_count) {
    const MinicBlock *block;
    size_t block_statement_index;

    if (block_id == MINIC_BLOCK_INVALID) {
        return true;
    }
    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        visited_blocks == NULL || statement_membership == NULL || block_id >= block_count) {
        return false;
    }
    if (visited_blocks[block_id]) {
        return true;
    }
    visited_blocks[block_id] = true;
    block = minic_c0_program_block(context->body->program, block_id);
    if (block == NULL) {
        return false;
    }
    for (block_statement_index = 0U;
         block_statement_index < block->statement_count;
         ++block_statement_index) {
        MinicStatementId statement_id;
        const MinicStatement *statement;

        statement_id = block->statements[block_statement_index];
        if (statement_id >= statement_count) {
            return false;
        }
        statement_membership[statement_id] = true;
        statement = minic_c0_program_statement(context->body->program, statement_id);
        if (statement == NULL ||
            !core_mark_block_statement_membership(context,
                                                  statement->then_block,
                                                  visited_blocks,
                                                  block_count,
                                                  statement_membership,
                                                  statement_count) ||
            !core_mark_block_statement_membership(context,
                                                  statement->else_block,
                                                  visited_blocks,
                                                  block_count,
                                                  statement_membership,
                                                  statement_count)) {
            return false;
        }
    }
    return true;
}

bool minic_core_lower_unreachable_external_reentry(
    const MinicCoreLowerContext *context,
    const MinicStatement *root_statement,
    MinicStatementId extra_statement_id) {
    const MinicC0Program *program;
    bool *visited_blocks;
    bool *statement_membership;
    bool root_found;
    bool unsafe;
    size_t source_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        root_statement == NULL) {
        return true;
    }
    program = context->body->program;
    if (program->statement_count == 0U ||
        program->block_count > SIZE_MAX / sizeof(*visited_blocks) ||
        program->statement_count > SIZE_MAX / sizeof(*statement_membership)) {
        return true;
    }
    visited_blocks = program->block_count == 0U
                         ? NULL
                         : (bool *)calloc(program->block_count, sizeof(*visited_blocks));
    statement_membership =
        (bool *)calloc(program->statement_count, sizeof(*statement_membership));
    if ((program->block_count != 0U && visited_blocks == NULL) ||
        statement_membership == NULL) {
        free(visited_blocks);
        free(statement_membership);
        return true;
    }

    root_found = false;
    for (source_index = 0U; source_index < program->statement_count; ++source_index) {
        const MinicStatement *candidate;

        candidate = minic_c0_program_statement(program, source_index);
        if (candidate == NULL) {
            free(visited_blocks);
            free(statement_membership);
            return true;
        }
        if (candidate == root_statement) {
            statement_membership[source_index] = true;
            root_found = true;
        }
    }
    if (!root_found ||
        (extra_statement_id != MINIC_STATEMENT_INVALID &&
         extra_statement_id >= program->statement_count) ||
        !core_mark_block_statement_membership(context,
                                              root_statement->then_block,
                                              visited_blocks,
                                              program->block_count,
                                              statement_membership,
                                              program->statement_count) ||
        !core_mark_block_statement_membership(context,
                                              root_statement->else_block,
                                              visited_blocks,
                                              program->block_count,
                                              statement_membership,
                                              program->statement_count)) {
        free(visited_blocks);
        free(statement_membership);
        return true;
    }
    if (extra_statement_id != MINIC_STATEMENT_INVALID) {
        statement_membership[extra_statement_id] = true;
    }

    unsafe = false;
    for (source_index = 0U; source_index < program->statement_count && !unsafe;
         ++source_index) {
        const MinicStatement *source;

        if (statement_membership[source_index]) {
            continue;
        }
        source = minic_c0_program_statement(program, source_index);
        if (source == NULL) {
            unsafe = true;
            break;
        }
        if (source->kind == MINIC_STATEMENT_GOTO &&
            source->target_statement < program->statement_count &&
            statement_membership[source->target_statement]) {
            unsafe = true;
            break;
        }
        if (source->kind == MINIC_STATEMENT_INLINE_ASM &&
            source->inline_asm_id < program->inline_asm_count) {
            const MinicInlineAsm *inline_asm;
            size_t label_index;

            inline_asm = &program->inline_asms[source->inline_asm_id];
            if (!inline_asm->is_goto) {
                continue;
            }
            for (label_index = 0U; label_index < inline_asm->label_count; ++label_index) {
                MinicStatementId target;

                target = inline_asm->labels[label_index].target_statement;
                if (target < program->statement_count && statement_membership[target]) {
                    unsafe = true;
                    break;
                }
            }
        }
    }

    free(visited_blocks);
    free(statement_membership);
    return unsafe;
}

/* A prior goto can pre-bind a label block inside an otherwise unreachable loop;
   pruning that loop would leave the target block unterminated. */
static bool core_unreachable_statement_has_bound_reentry(
    const MinicCoreLowerContext *context,
    const MinicStatement *root_statement,
    MinicStatementId extra_statement_id) {
    const MinicC0Program *program;
    bool *visited_blocks;
    bool *statement_membership;
    bool root_found;
    bool has_reentry;
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->statement_blocks == NULL || root_statement == NULL) {
        return false;
    }
    program = context->body->program;
    if (program->statement_count == 0U ||
        context->statement_block_count < program->statement_count ||
        program->block_count > SIZE_MAX / sizeof(*visited_blocks) ||
        program->statement_count > SIZE_MAX / sizeof(*statement_membership)) {
        return false;
    }
    visited_blocks = program->block_count == 0U
                         ? NULL
                         : (bool *)calloc(program->block_count, sizeof(*visited_blocks));
    statement_membership =
        (bool *)calloc(program->statement_count, sizeof(*statement_membership));
    if ((program->block_count != 0U && visited_blocks == NULL) ||
        statement_membership == NULL) {
        free(visited_blocks);
        free(statement_membership);
        return false;
    }

    root_found = false;
    for (index = 0U; index < program->statement_count; ++index) {
        const MinicStatement *candidate = minic_c0_program_statement(program, index);
        if (candidate == NULL) {
            free(visited_blocks);
            free(statement_membership);
            return false;
        }
        if (candidate == root_statement) {
            statement_membership[index] = true;
            root_found = true;
        }
    }
    if (!root_found ||
        !core_mark_block_statement_membership(context,
                                              root_statement->then_block,
                                              visited_blocks,
                                              program->block_count,
                                              statement_membership,
                                              program->statement_count) ||
        !core_mark_block_statement_membership(context,
                                              root_statement->else_block,
                                              visited_blocks,
                                              program->block_count,
                                              statement_membership,
                                              program->statement_count)) {
        free(visited_blocks);
        free(statement_membership);
        return false;
    }
    if (extra_statement_id != MINIC_STATEMENT_INVALID &&
        extra_statement_id < program->statement_count) {
        statement_membership[extra_statement_id] = true;
    }

    has_reentry = false;
    for (index = 0U; index < program->statement_count; ++index) {
        if (index != extra_statement_id && statement_membership[index] &&
            context->statement_blocks[index] != MINIC_CORE_BLOCK_INVALID) {
            has_reentry = true;
            break;
        }
    }
    free(visited_blocks);
    free(statement_membership);
    return has_reentry;
}

/* M144_UNREFERENCED_LOOP_LABEL_METADATA_OWNER: parser loop normalization can
   leave an otherwise-empty label at the condition tail even when no source
   continue/goto refers to it.  minic_core_loop_internal_while_label_pair() gives this label a
   strong identity: its source position is exactly that of one normalized WHILE.
   Treat it as non-executable parser metadata only when that owner is unique and
   the label has no direct goto, asm-goto, or &&label reference anywhere in the
   function program. Any real control-flow use remains owned by ordinary LABEL /
   GOTO lowering and therefore stays fail-closed here. */
static bool core_unreferenced_internal_loop_label(
    const MinicCoreLowerContext *context,
    const MinicStatement *label,
    MinicStatementId label_id) {
    const MinicC0Program *program;
    size_t loop_matches;
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        label == NULL || label->kind != MINIC_STATEMENT_LABEL) {
        return false;
    }
    program = context->body->program;
    if (label_id >= program->statement_count) {
        return false;
    }

    if (minic_core_reentry_index_ready(context->reentry_index, program)) {
        /* Exact early-exit semantics: referenced labels are never
         * optimization-only internal while metadata. */
        if (!minic_core_reentry_index_unreferenced_loop_label(
                context->reentry_index, program, label_id))
            return false;
        loop_matches = 0U;
        for (index = 0U; index < program->statement_count; ++index) {
            const MinicStatement *candidate =
                minic_c0_program_statement(program, index);
            if (candidate == NULL) return false;
            if (candidate->kind == MINIC_STATEMENT_WHILE &&
                minic_core_loop_internal_while_label_pair(label, candidate)) {
                ++loop_matches;
                if (loop_matches > 1U) return false;
            }
        }
        return loop_matches == 1U;
    }

    loop_matches = 0U;
    for (index = 0U; index < program->statement_count; ++index) {
        const MinicStatement *candidate = minic_c0_program_statement(program, index);
        if (candidate == NULL) {
            return false;
        }
        if (candidate->kind == MINIC_STATEMENT_WHILE &&
            minic_core_loop_internal_while_label_pair(label, candidate)) {
            loop_matches += 1U;
            if (loop_matches > 1U) {
                return false;
            }
        }
        if (candidate->kind == MINIC_STATEMENT_GOTO &&
            candidate->target_statement == label_id) {
            return false;
        }
        if (candidate->kind == MINIC_STATEMENT_INLINE_ASM &&
            candidate->inline_asm_id < program->inline_asm_count) {
            const MinicInlineAsm *inline_asm = &program->inline_asms[candidate->inline_asm_id];
            size_t label_index;
            if (inline_asm->is_goto) {
                for (label_index = 0U; label_index < inline_asm->label_count; ++label_index) {
                    if (inline_asm->labels[label_index].target_statement == label_id) {
                        return false;
                    }
                }
            }
        }
    }
    if (loop_matches != 1U) {
        return false;
    }
    for (index = 0U; index < program->expression_count; ++index) {
        const MinicExpression *expression = minic_c0_program_expression(program, index);
        if (expression == NULL) {
            return false;
        }
        if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS &&
            expression->value.label_statement_id == label_id) {
            return false;
        }
    }
    return true;
}


MinicCoreLowerStatus
minic_core_lower_internal_lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated) {
    size_t statement_index;
    bool block_terminated;

    if (context == NULL || source_block == NULL || terminated == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    block_terminated = false;
    for (statement_index = 0U; statement_index < source_block->statement_count; ++statement_index) {
        const MinicStatement *statement;
        MinicCoreLowerStatus status;
        bool statement_terminated;

        statement = minic_c0_program_statement(context->body->program,
                                               source_block->statements[statement_index]);
        if (statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (statement->kind == MINIC_STATEMENT_LABEL &&
            core_unreferenced_internal_loop_label(
                context, statement, source_block->statements[statement_index])) {
            continue;
        }
        if (block_terminated) {
            /* Parser scope exit may materialize cleanup/return tails after an
               already-terminating edge. More generally, statements on this
               path are unreachable and need no Core instructions. A parser
               internal while label is paired with the following loop; prune
               the pair as one subtree so the loop's own backedge does not look
               like external re-entry. Preserve fail-closed behavior only for
               a goto/asm-goto originating outside the pruned subtree. */
            if (statement->kind == MINIC_STATEMENT_RETURN ||
                core_is_materialized_cleanup_statement(context, statement)) {
                continue;
            }
            if (statement->kind == MINIC_STATEMENT_LABEL &&
                statement_index + 1U < source_block->statement_count) {
                const MinicStatement *unreachable_loop;
                MinicStatementId unreachable_loop_id;

                unreachable_loop_id = source_block->statements[statement_index + 1U];
                unreachable_loop =
                    minic_c0_program_statement(context->body->program, unreachable_loop_id);
                if (minic_core_loop_internal_while_label_pair(statement, unreachable_loop)) {
                    if (minic_core_lower_unreachable_external_reentry(
                            context,
                            unreachable_loop,
                            source_block->statements[statement_index]) ||
                        core_unreachable_statement_has_bound_reentry(
                            context,
                            unreachable_loop,
                            source_block->statements[statement_index])) {
                        MinicCoreBlockId detached_preheader;

                        /* A source goto may legally target a user label inside
                           an otherwise unreachable loop body. Build the loop
                           CFG from an orphan preheader so its condition/backedge
                           structure exists without replacing the already-
                           terminated outer path. The externally referenced user
                           label has already been pre-bound by ordinary goto
                           lowering and becomes the real entry edge. */
                        if (!minic_core_function_add_block(
                                context->function, &detached_preheader)) {
                            return MINIC_CORE_LOWER_ERROR;
                        }
                        context->block_id = detached_preheader;
                        status = minic_core_lower_loop_cfg(
                            context,
                            unreachable_loop,
                            source_block->statements[statement_index],
                            &statement_terminated);
                        if (status != MINIC_CORE_LOWER_OK) {
                            return status;
                        }
                        block_terminated = statement_terminated;
                        statement_index += 1U;
                        continue;
                    }
                    statement_index += 1U;
                    continue;
                }
            }
            if (statement->kind == MINIC_STATEMENT_WHILE &&
                (minic_core_lower_unreachable_external_reentry(
                     context, statement, MINIC_STATEMENT_INVALID) ||
                 core_unreachable_statement_has_bound_reentry(
                     context, statement, MINIC_STATEMENT_INVALID))) {
                MinicCoreBlockId detached_preheader;

                if (!minic_core_function_add_block(
                        context->function, &detached_preheader)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                context->block_id = detached_preheader;
                status = minic_core_lower_loop_cfg(
                    context, statement, MINIC_STATEMENT_INVALID, &statement_terminated);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                block_terminated = statement_terminated;
                continue;
            }
            if (statement->kind != MINIC_STATEMENT_LABEL &&
                statement->kind != MINIC_STATEMENT_CASE &&
                statement->kind != MINIC_STATEMENT_DEFAULT) {
                if (minic_core_lower_unreachable_external_reentry(
                        context, statement, MINIC_STATEMENT_INVALID)) {
                    MinicCoreBlockId detached_preheader;
                    if (!minic_core_function_add_block(
                            context->function, &detached_preheader)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                    context->block_id = detached_preheader;
                    block_terminated = false;
                } else {
                    continue;
                }
            }
        }
        /* BATCH_C_ZERO_DISTANCE_CLEANUP_EDGE: cleanup ids are semantic edge
           metadata. Equal ids mean the edge crosses no cleanup lifetime, even
           when both ids are non-root. Only an actual context transition needs
           cleanup-expression lowering, which remains fail-closed here. */
        /* M142_NONEDGE_CLEANUP_METADATA_OWNER: cleanup ids describe the
           current lifetime context as well as executable cleanup transitions.
           ASSIGN and LABEL have no outgoing control edge of their own: the
           eventual RETURN/BREAK/GOTO/scope-exit owns cleanup execution. GNU
           source labels inside an active cleanup context must carry their
           actual context for a goto to resolve the correct lifetime boundary.
           Keep other nonzero-distance shapes fail-closed. */
        if (statement->cleanup_context != statement->cleanup_stop_context &&
            statement->kind != MINIC_STATEMENT_RETURN &&
            statement->kind != MINIC_STATEMENT_ASSIGN &&
            statement->kind != MINIC_STATEMENT_BREAK &&
            statement->kind != MINIC_STATEMENT_LABEL &&
            !(statement->kind == MINIC_STATEMENT_GOTO &&
              statement->expression == MINIC_EXPRESSION_INVALID &&
              statement->target_statement != MINIC_STATEMENT_INVALID)) {
            (void)fprintf(stderr,
                          "CORE_FAST_TRACE stage=statement reason=cleanup-context "
                          "function=%s kind=%d span=%zu:%zu cleanup=%llu stop=%llu\n",
                          context->source_function->name,
                          (int)statement->kind,
                          statement->span.begin.line,
                          statement->span.begin.column,
                          (unsigned long long)statement->cleanup_context,
                          (unsigned long long)statement->cleanup_stop_context);
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        statement_terminated = false;
        if (statement->kind == MINIC_STATEMENT_CASE ||
            statement->kind == MINIC_STATEMENT_DEFAULT) {
            MinicCoreBlockId label_block;
            MinicStatementId label_statement_id =
                source_block->statements[statement_index];

            /* Only lower case/default as generic re-entry labels when the
               enclosing nested-switch fallback pre-bound this exact statement.
               The established top-level switch segment path remains unchanged. */
            if (context->statement_blocks == NULL ||
                label_statement_id >= context->statement_block_count ||
                context->statement_blocks[label_statement_id] ==
                    MINIC_CORE_BLOCK_INVALID) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = ensure_statement_block(
                context, label_statement_id, &label_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (!block_terminated && context->block_id != label_block) {
                status = minic_core_lower_internal_set_branch(
                    context, context->block_id, statement->span, label_block);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            context->block_id = label_block;
            /* Nested switch fallback emits CASE/DEFAULT as ordinary reentry
             * labels in the shared source-block walker. A dispatch edge may
             * enter this label without executing its lexical predecessor;
             * facts learned in the previous case are therefore not valid.
             * The top-level switch segment owner performs its own precise
             * dispatch/fallthrough meet and never visits these label nodes. */
            minic_core_local_facts_clear_known(context);
        } else if (statement->kind == MINIC_STATEMENT_LABEL) {
            const MinicStatement *loop = NULL;
            bool internal_loop_label = false;
            /* C labels are control-flow join points. A backward goto can
               invalidate all local facts learned on the first arrival.
               Keep the IR branches; only drop speculative local facts. */
            minic_core_local_facts_clear_known(context);
            if (statement_index + 1U < source_block->statement_count) {
                MinicStatementId next_statement_id = source_block->statements[statement_index + 1U];
                loop = minic_c0_program_statement(context->body->program, next_statement_id);
                internal_loop_label = minic_core_loop_internal_while_label_pair(statement, loop);
            }
            if (internal_loop_label) {
                status = minic_core_lower_loop_cfg(context,
                                     loop,
                                     source_block->statements[statement_index],
                                     &statement_terminated);
                if (status != MINIC_CORE_LOWER_OK) return status;
                statement_index += 1U;
            } else {
                MinicCoreBlockId label_block;
                MinicStatementId label_statement_id = source_block->statements[statement_index];
                if (statement->target_expression != MINIC_EXPRESSION_INVALID || statement->expression != MINIC_EXPRESSION_INVALID || statement->target_statement != MINIC_STATEMENT_INVALID) return MINIC_CORE_LOWER_UNSUPPORTED;
                status = ensure_statement_block(context, label_statement_id, &label_block);
                if (status != MINIC_CORE_LOWER_OK) return status;
                if (!block_terminated && context->block_id != label_block) {
                    status = minic_core_lower_internal_set_branch(context, context->block_id, statement->span, label_block);
                    if (status != MINIC_CORE_LOWER_OK) return status;
                }
                context->block_id = label_block;
                /* M190_USER_LABEL_FACT_BARRIER: a goto target is a CFG join,
                   not a straight-line transfer from its lexical predecessor. */
                minic_core_local_facts_clear_known(context);
            }
        } else {
            switch (statement->kind) {
            case MINIC_STATEMENT_ASSIGN:
                status = minic_core_lower_assignment(context, statement);
                break;
            case MINIC_STATEMENT_RECORD_COPY:
            case MINIC_STATEMENT_RECORD_INITIALIZE:
                status = minic_core_lower_record_copy_statement(context, statement);
                break;
            case MINIC_STATEMENT_EXPRESSION:
                status = minic_core_lower_internal_lower_expression_statement(context, statement);
                if (status == MINIC_CORE_LOWER_OK &&
                    context->block_id < context->function->block_count &&
                    context->function->blocks[context->block_id].has_terminator) {
                    statement_terminated = true;
                }
                break;
            case MINIC_STATEMENT_INLINE_ASM:
                status = minic_core_lower_inline_asm(context, statement);
                minic_core_local_facts_clear_known(context);
                break;
            case MINIC_STATEMENT_RETURN:
                status = lower_return(context, statement);
                statement_terminated = status == MINIC_CORE_LOWER_OK;
                break;
            case MINIC_STATEMENT_BREAK:
                if (context->break_target == MINIC_CORE_BLOCK_INVALID) {
                    status = MINIC_CORE_LOWER_UNSUPPORTED;
                    break;
                }
                if (statement->cleanup_context != statement->cleanup_stop_context) {
                    status = lower_cleanup_contexts(
                        context, statement->cleanup_context, statement->cleanup_stop_context);
                    if (status != MINIC_CORE_LOWER_OK) {
                        break;
                    }
                }
                status = minic_core_lower_internal_set_branch(
                    context, context->block_id, statement->span, context->break_target);
                statement_terminated = status == MINIC_CORE_LOWER_OK;
                break;
            case MINIC_STATEMENT_GOTO: {
                const MinicStatement *target_statement;
                MinicCoreBlockId target_block;

                /* M158_FINAL_STRICT_TAIL_COMPUTED_GOTO: &&label already lowers
                   to BLOCK_ADDRESS.  Preserve GNU `goto *expr` as a first-class
                   Core CFG edge instead of pretending it is an ordinary branch. */
                if (statement->expression != MINIC_EXPRESSION_INVALID &&
                    statement->target_statement == MINIC_STATEMENT_INVALID) {
                    MinicCoreTerminator terminator;
                    MinicCoreValueId target_value;

                    if (statement->target_expression != MINIC_EXPRESSION_INVALID) {
                        status = MINIC_CORE_LOWER_UNSUPPORTED;
                        break;
                    }
                    status = lower_expression(
                        context, statement->expression, &target_value);
                    if (status != MINIC_CORE_LOWER_OK) {
                        break;
                    }
                    if (target_value >= context->function->value_count ||
                        !minic_type_is_pointer(context->function->values[target_value].type)) {
                        status = MINIC_CORE_LOWER_UNSUPPORTED;
                        break;
                    }
                    (void)memset(&terminator, 0, sizeof(terminator));
                    terminator.kind = MINIC_CORE_TERMINATOR_INDIRECT_BRANCH;
                    terminator.span = statement->span;
                    terminator.return_value = MINIC_CORE_VALUE_INVALID;
                    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
                    terminator.indirect_target = target_value;
                    status = minic_core_function_set_terminator(
                                 context->function, context->block_id, &terminator)
                                 ? MINIC_CORE_LOWER_OK
                                 : MINIC_CORE_LOWER_ERROR;
                    statement_terminated = status == MINIC_CORE_LOWER_OK;
                    break;
                }
                if (statement->expression != MINIC_EXPRESSION_INVALID ||
                    statement->target_statement == MINIC_STATEMENT_INVALID) {
                    status = MINIC_CORE_LOWER_UNSUPPORTED;
                    break;
                }
                target_statement = minic_c0_program_statement(
                    context->body->program, statement->target_statement);
                if (target_statement == NULL ||
                    target_statement->kind != MINIC_STATEMENT_LABEL) {
                    status = MINIC_CORE_LOWER_ERROR;
                    break;
                }
                if (statement->cleanup_context != statement->cleanup_stop_context) {
                    status = lower_cleanup_contexts(
                        context, statement->cleanup_context, statement->cleanup_stop_context);
                    if (status != MINIC_CORE_LOWER_OK) {
                        break;
                    }
                }
                status = ensure_statement_block(
                    context, statement->target_statement, &target_block);
                if (status == MINIC_CORE_LOWER_OK) {
                    status = minic_core_lower_internal_set_branch(
                        context, context->block_id, statement->span, target_block);
                }
                statement_terminated = status == MINIC_CORE_LOWER_OK;
                break;
            }
            case MINIC_STATEMENT_IF:
                status = minic_core_lower_if(context, statement, &statement_terminated);
                break;
            case MINIC_STATEMENT_WHILE:
                status = minic_core_lower_loop_statement(context, statement, &statement_terminated);
                break;
            case MINIC_STATEMENT_SWITCH:
                minic_core_local_facts_clear_known(context);
                status = minic_core_lower_switch(context, statement, &statement_terminated);
                minic_core_local_facts_clear_known(context);
                break;
            default:
                (void)fprintf(stderr,
                              "CORE_FAST_TRACE stage=statement reason=unsupported-kind "
                              "function=%s kind=%d span=%zu:%zu break_target=%llu\n",
                              context->source_function->name,
                              (int)statement->kind,
                              statement->span.begin.line,
                              statement->span.begin.column,
                              (unsigned long long)context->break_target);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            if (status != MINIC_CORE_LOWER_OK) {
                (void)fprintf(stderr,
                              "CORE_FAST_TRACE stage=statement reason=lower-status "
                              "function=%s kind=%d status=%d span=%zu:%zu break_target=%llu\n",
                              context->source_function->name,
                              (int)statement->kind,
                              (int)status,
                              statement->span.begin.line,
                              statement->span.begin.column,
                              (unsigned long long)context->break_target);
                return status;
            }
        }
        block_terminated = statement_terminated;
    }
    *terminated = block_terminated;
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_function_with_index(const MinicFunctionBodyView *body,
                                               const MinicTargetInfo *target,
                                               MinicCoreFunction *output,
                                               MinicCoreReentryIndex *reentry_index) {
    const MinicFunction *source_function;
    const MinicBlock *source_block;
    MinicCoreFunction lowered;
    MinicCoreLowerContext context;
    MinicCoreBlockId block_id;
    MinicCoreObjectId *local_objects;
    MinicCoreLocalIntegerConstant *local_integer_constants;
    MinicCoreBlockId *statement_blocks;
    MinicCoreLowerStatus status;
    size_t local_index;
    size_t statement_index;
    bool terminated;

    if (body == NULL || body->program == NULL || target == NULL || output == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source_function = minic_c0_function_body_function(body);
    source_block = minic_c0_program_block(body->program, minic_c0_function_body_root_block(body));
    if (source_function == NULL || source_block == NULL || source_function->name == NULL ||
        source_function->name_length == 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (source_block->statement_count == 0U && !minic_type_is_void(source_function->return_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (source_function->local_count > SIZE_MAX / sizeof(*local_objects)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    local_objects =
        source_function->local_count == 0U
            ? NULL
            : (MinicCoreObjectId *)malloc(source_function->local_count * sizeof(*local_objects));
    if (source_function->local_count != 0U && local_objects == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    local_integer_constants =
        source_function->local_count == 0U
            ? NULL
            : (MinicCoreLocalIntegerConstant *)calloc(
                  source_function->local_count, sizeof(*local_integer_constants));
    if (source_function->local_count != 0U && local_integer_constants == NULL) {
        free(local_objects);
        return MINIC_CORE_LOWER_ERROR;
    }
    for (local_index = 0U; local_index < source_function->local_count; ++local_index) local_objects[local_index] = MINIC_CORE_OBJECT_INVALID;
    if (body->program->statement_count > SIZE_MAX / sizeof(*statement_blocks)) { free(local_integer_constants); free(local_objects); return MINIC_CORE_LOWER_ERROR; }
    statement_blocks = body->program->statement_count == 0U ? NULL : (MinicCoreBlockId *)malloc(body->program->statement_count * sizeof(*statement_blocks));
    if (body->program->statement_count != 0U && statement_blocks == NULL) { free(local_integer_constants); free(local_objects); return MINIC_CORE_LOWER_ERROR; }
    for (statement_index = 0U; statement_index < body->program->statement_count; ++statement_index) statement_blocks[statement_index] = MINIC_CORE_BLOCK_INVALID;

    minic_core_function_initialize(&lowered);
    if (!minic_core_function_set_signature(&lowered,
                                           source_function->name,
                                           source_function->name_length,
                                           source_function->return_type,
                                           source_function->parameter_types,
                                           source_function->parameter_count) ||
        !minic_core_function_set_calling_convention(
            &lowered, source_function->calling_convention) ||
        !minic_core_function_add_block(&lowered, &block_id)) {
        free(statement_blocks); free(local_integer_constants); free(local_objects); minic_core_function_destroy(&lowered); return MINIC_CORE_LOWER_ERROR;
    }
    /* C allows a function prototype and its definition to materialize
     * distinct but compatible pointer-to-array descriptor identities.
     * Canonicalize ONLY at the AST -> Core ingress boundary: the Core IR
     * signature must use the actual definition parameter-local type so its
     * PARAMETER instruction and STORE remain exactly identical. Preserve
     * Core verification's strict type identity and discard only the formal
     * parameter's top-level C qualifiers, as required by function types. */
    {
        size_t parameter_index;
        for (parameter_index = 0U; parameter_index < source_function->parameter_count;
             ++parameter_index) {
            const MinicLocal *parameter;
            MinicType normalized;
            if (source_function->local_begin >= body->program->local_count ||
                parameter_index >= body->program->local_count - source_function->local_begin) {
                free(statement_blocks); free(local_integer_constants); free(local_objects);
                minic_core_function_destroy(&lowered);
                return MINIC_CORE_LOWER_ERROR;
            }
            parameter = minic_c0_program_local(
                body->program, source_function->local_begin + parameter_index);
            if (parameter == NULL ||
                !minic_type_unqualified(parameter->type, &normalized) ||
                !minic_c0_types_compatible(
                    body->program, source_function->parameter_types[parameter_index],
                    normalized)) {
                free(statement_blocks); free(local_integer_constants); free(local_objects);
                minic_core_function_destroy(&lowered);
                return MINIC_CORE_LOWER_ERROR;
            }
            lowered.parameter_types[parameter_index] = normalized;
        }
    }
    (void)memset(&context, 0, sizeof(context));
    context.body = body;
    context.source_function = source_function;
    context.target = target;
    context.reentry_index = reentry_index;
    context.function = &lowered;
    lowered.suppress_source_label_aliases = source_function->is_integer_specialization;
    context.block_id = block_id;
    context.break_target = MINIC_CORE_BLOCK_INVALID;
    context.local_objects = local_objects;
    context.local_integer_constants = local_integer_constants;
    context.statement_blocks = statement_blocks;
    context.statement_block_count = body->program->statement_count;
    status = lower_parameter_ingress(&context);
    terminated = false;
    if (status == MINIC_CORE_LOWER_OK) status = minic_core_lower_internal_lower_block(&context, source_block, &terminated);
    free(statement_blocks); free(local_integer_constants); free(local_objects);
    context.local_integer_constants = NULL;
    if (status != MINIC_CORE_LOWER_OK) {
        minic_core_function_destroy(&lowered);
        return status;
    }
    if (!terminated && minic_type_is_void(source_function->return_type)) {
        MinicCoreTerminator terminator;

        (void)memset(&terminator, 0, sizeof(terminator));
        terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
        terminator.return_value = MINIC_CORE_VALUE_INVALID;
        if (!minic_core_function_set_terminator(&lowered, context.block_id, &terminator)) {
            minic_core_function_destroy(&lowered);
            return MINIC_CORE_LOWER_ERROR;
        }
        terminated = true;
    }
    if (!terminated) {
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!core_capture_enum_metadata(&context) || !minic_core_function_verify(&lowered)) {
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_ERROR;
    }
    minic_core_function_destroy(output);
    *output = lowered;
    return MINIC_CORE_LOWER_OK;
}

/* Standalone Core IR clients retain conservative no-index lowering. */
MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,
                                               const MinicTargetInfo *target,
                                               MinicCoreFunction *output) {
    return minic_core_lower_function_with_index(body, target, output, NULL);
}
