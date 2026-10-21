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
static MinicCoreLowerStatus lower_record_compound_literal_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *object_id);
static MinicCoreLowerStatus lower_record_conditional_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *result_object);
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

static MinicCoreLowerStatus lower_local_object(MinicCoreLowerContext *context,
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
            !minic_type_equal(parameter_value_type,
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
        status = lower_local_object(context, local_id, &object_id);
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

MinicCoreLowerStatus lower_address(MinicCoreLowerContext *context,
                                          MinicExpressionId expression_id,
                                          MinicCoreValueId *address_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
    MinicCoreObjectId object_id;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || address_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_ALLOCA) {
        MinicCoreValueId size_value;
        MinicType void_pointer;

        status = lower_expression(context, expression->value.unary.operand, &size_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (size_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[size_value].type,
                              minic_type_unsigned_long()) ||
            !minic_type_pointer_to(minic_type_void(), &void_pointer) ||
            !minic_type_equal(expression->type, void_pointer)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC;
        instruction.span = expression->span;
        instruction.type = void_pointer;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = size_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value_category != MINIC_VALUE_LVALUE) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* Array compound literals reuse the legacy repeated-local storage model
       until frontend array representation convergence. Execute their hidden
       initializer block at the expression point, then expose element-zero
       storage exactly like an ordinary local array object. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL) {
        MinicArrayObjectInfo array_info;

        (void)memset(&array_info, 0, sizeof(array_info));
        if (minic_c0_expression_array_object_info(
                context->body->program, expression, &array_info) &&
            !array_info.has_materialized_type) {
            const MinicBlock *initializer_block;
            const MinicLocal *local;
            bool terminated;

            local = minic_c0_program_local(
                context->body->program, expression->value.compound_literal.local_id);
            initializer_block = minic_c0_program_block(
                context->body->program, expression->value.compound_literal.initializer_block);
            if (local == NULL || initializer_block == NULL || !local->is_array ||
                local->is_register_storage || local->element_count == 0U ||
                !minic_type_equal(local->type, expression->type) ||
                !minic_type_equal(array_info.element_type, expression->type) ||
                array_info.element_count != local->element_count) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            terminated = false;
            status = minic_core_lower_internal_lower_block(context, initializer_block, &terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (terminated) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_local_object(
                context, expression->value.compound_literal.local_id, &object_id);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (object_id >= context->function->object_count ||
                !minic_type_equal(context->function->objects[object_id].type, local->type) ||
                context->function->objects[object_id].element_count != local->element_count) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
            instruction.span = expression->span;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.object_id = object_id;
            if (!minic_type_pointer_to(expression->type, &instruction.type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, address_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    /* M157_SCALAR_COMPOUND_LITERAL_ADDRESS_OWNER: scalar compound literals
       use the same frontend-owned hidden local + initializer block model as
       record compound literals.  Execute that initializer at the expression
       point, reuse the hidden local's Core object, and expose its address so
       the ordinary scalar lvalue-read path performs the final load.  No scalar
       literal value is synthesized separately from its addressable C object. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL &&
        core_memory_scalar_type(expression->type)) {
        const MinicBlock *initializer_block;
        const MinicLocal *local;
        bool terminated;

        local = minic_c0_program_local(
            context->body->program, expression->value.compound_literal.local_id);
        initializer_block = minic_c0_program_block(
            context->body->program, expression->value.compound_literal.initializer_block);
        if (local == NULL || initializer_block == NULL || local->is_array ||
            local->is_register_storage || !minic_type_equal(local->type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        terminated = false;
        status = minic_core_lower_internal_lower_block(context, initializer_block, &terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_local_object(
            context, expression->value.compound_literal.local_id, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (object_id >= context->function->object_count ||
            !minic_type_equal(context->function->objects[object_id].type, local->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = object_id;
        if (!minic_type_pointer_to(expression->type, &instruction.type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    /* BATCH_U_RECORD_COMPOUND_LITERAL_ADDRESS: a record compound literal is
       an lvalue with a real semantic backing object.  Reuse that object for
       address-of just as the address-backed aggregate seam already does; do
       not synthesize a second temporary and do not special-case call sites. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL &&
        minic_type_is_record(expression->type)) {
        status = lower_record_compound_literal_object(context, expression, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = object_id;
        if (!minic_type_pointer_to(expression->type, &instruction.type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local =
            minic_c0_program_local(context->body->program, expression->value.local_id);

        if (local == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (local->is_array && local->element_count == 0U) {
            const MinicLocal *address_local;
            MinicCoreValueId slot_address;
            MinicType expected_pointer;

            if (local->dynamic_address_local_id == MINIC_LOCAL_INVALID ||
                local->dynamic_address_local_id >= context->body->program->local_count ||
                !minic_type_pointer_to(expression->type, &expected_pointer)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            address_local = minic_c0_program_local(
                context->body->program, local->dynamic_address_local_id);
            if (address_local == NULL || address_local->is_array ||
                address_local->element_count != 1U ||
                !minic_type_equal(address_local->type, expected_pointer)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_local_object(
                context, local->dynamic_address_local_id, &object_id);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
            instruction.span = expression->span;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.object_id = object_id;
            if (!minic_type_pointer_to(address_local->type, &instruction.type) ||
                !minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &slot_address)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
            instruction.span = expression->span;
            instruction.type = address_local->type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.load.address = slot_address;
            instruction.value.load.is_volatile = false;
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, address_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }

        status = lower_local_object(context, expression->value.local_id, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = object_id;
        if (!minic_type_pointer_to(expression->type, &instruction.type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *global;
        MinicCoreGlobalId global_id;

        global = minic_c0_program_global_object(context->body->program,
                                                expression->value.global_object_id);
        if (global == NULL || global->name == NULL || global->name_length == 0U) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_type_equal(global->type, expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER: GNU C permits linker-defined
           declaration-only `extern void` symbols such as __start_notes.  They
           have an address but no C object value to load/store.  Keep ordinary
           object addressability unchanged and admit only an extern, non-
           tentative, initializer-free void declaration at this source boundary. */
        if (!core_global_addressable_type(global->type) &&
            !(minic_type_is_void(global->type) && global->is_extern &&
              !global->is_tentative && global->initializer_count == 0U &&
              global->relocation_count == 0U && global->union_selection_count == 0U)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_core_function_add_global(
                context->function, global->name, global->name_length, global->type, &global_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS;
        instruction.span = expression->span;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.global_id = global_id;
        if (!minic_type_pointer_to(expression->type, &instruction.type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_DEREFERENCE) {
        MinicCoreValueId pointer_id;
        MinicType expected_pointer;

        if (!minic_type_pointer_to(expression->type, &expected_pointer)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_expression(context, expression->value.unary.operand, &pointer_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (pointer_id >= context->function->value_count ||
            !minic_type_equal(context->function->values[pointer_id].type, expected_pointer)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        *address_id = pointer_id;
        return MINIC_CORE_LOWER_OK;
    }
    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base;
        const MinicExpression *index;
        MinicArrayObjectInfo array_info;
        MinicCoreInstruction offset_instruction;
        MinicCoreObjectId base_object;
        MinicCoreValueId base_value;
        MinicCoreValueId index_value;
        MinicCoreLowerStatus subscript_status;
        MinicType array_pointer_type;
        MinicType base_value_type;
        MinicType element_type;
        MinicType index_value_type;
        MinicType pointer_type;
        size_t element_size;
        bool array_base;

        base =
            minic_c0_program_expression(context->body->program, expression->value.subscript.base);
        index =
            minic_c0_program_expression(context->body->program, expression->value.subscript.index);
        /* M66_SUBSCRIPT_INDEX_VALUE_TYPE: an lvalue-to-rvalue conversion strips
           top-level qualifiers. Validate the Core value against that semantic
           value type, not against the qualified lvalue type carried by the AST
           node (for example `const size_t index`). */
        if (base == NULL || index == NULL ||
            !core_scalar_expression_value_type(context->body, index, &index_value_type) ||
            !minic_type_is_integer(index_value_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&array_info, 0, sizeof(array_info));
        array_base = minic_c0_expression_array_object_info(
            context->body->program, base, &array_info);
        if (array_base) {
            /* M71_LEGACY_ARRAY_OBJECT_SUBSCRIPT: array-object metadata has two
               valid frontend representations while array type convergence is
               still in progress. Materialized arrays carry an array MinicType;
               legacy local/member arrays carry the element type plus explicit
               array-object metadata. Both denote the same C array object and
               must form the address of element zero without loading the array. */
            if (!minic_type_equal(array_info.element_type, expression->type) ||
                !minic_type_pointer_to(array_info.element_type, &pointer_type) ||
                !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                          core_data_layout(context),
                                                          pointer_type,
                                                          &element_size)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            subscript_status =
                lower_address(context, expression->value.subscript.base, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
            if (base_value >= context->function->value_count) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (array_info.has_materialized_type) {
                if (!minic_type_is_array(base->type) ||
                    !minic_type_pointer_to(base->type, &array_pointer_type) ||
                    !minic_type_equal(context->function->values[base_value].type,
                                      array_pointer_type)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                subscript_status = append_scalar_bitcast(
                    context, base->span, pointer_type, base_value, &base_value);
                if (subscript_status != MINIC_CORE_LOWER_OK) {
                    return subscript_status;
                }
            } else if (!minic_type_equal(base->type, array_info.element_type) ||
                       !minic_type_equal(context->function->values[base_value].type,
                                         pointer_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
        } else {
            /* Pointer-valued lvalues undergo lvalue-to-rvalue conversion before
               subscript arithmetic.  In particular, a pointer member selected
               through a const record carries qualified storage type in the AST
               while its scalar value type is unqualified.  Validate and carry
               that semantic value type rather than requiring storage identity. */
            if (!core_scalar_expression_value_type(context->body, base, &base_value_type) ||
                !minic_type_is_pointer(base_value_type) ||
                !minic_type_pointee(base_value_type, &element_type) ||
                !minic_type_equal(element_type, expression->type) ||
                !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                          core_data_layout(context),
                                                          base_value_type,
                                                          &element_size)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            pointer_type = base_value_type;
            subscript_status =
                lower_expression(context, expression->value.subscript.base, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                (void)fprintf(stderr,
                              "CORE_SUBSCRIPT_STAGE function=%s stage=pointer-base status=%d\n",
                              context->source_function != NULL ? context->source_function->name : "?",
                              (int)subscript_status);
                return subscript_status;
            }
            if (base_value >= context->function->value_count ||
                !minic_type_equal(context->function->values[base_value].type, base_value_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
        }
        subscript_status =
            minic_core_lower_internal_spill_scalar_value(context, base->span, pointer_type, base_value, &base_object);
        if (subscript_status != MINIC_CORE_LOWER_OK) {
            return subscript_status;
        }
        subscript_status =
            lower_expression(context, expression->value.subscript.index, &index_value);
        if (subscript_status != MINIC_CORE_LOWER_OK) {
            const MinicExpression *index_expression =
                minic_c0_program_expression(context->body->program,
                                            expression->value.subscript.index);
            (void)fprintf(stderr,
                          "CORE_SUBSCRIPT_STAGE function=%s stage=index status=%d index_kind=%d\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          (int)subscript_status,
                          index_expression != NULL ? (int)index_expression->kind : -1);
            return subscript_status;
        }
        if (index_value >= context->function->value_count ||
            !minic_type_is_integer(context->function->values[index_value].type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_type_equal(context->function->values[index_value].type, index_value_type)) {
            subscript_status = minic_core_lower_internal_append_integer_conversion(
                context, index->span, index_value_type, index_value, &index_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
        }
        subscript_status =
            minic_core_lower_internal_reload_scalar_value(context, base->span, pointer_type, base_object, &base_value);
        if (subscript_status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr,
                          "CORE_SUBSCRIPT_STAGE function=%s stage=reload-base status=%d\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          (int)subscript_status);
            return subscript_status;
        }

        (void)memset(&offset_instruction, 0, sizeof(offset_instruction));
        offset_instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        offset_instruction.span = expression->span;
        offset_instruction.type = pointer_type;
        offset_instruction.result = MINIC_CORE_VALUE_INVALID;
        offset_instruction.value.pointer_offset.base = base_value;
        offset_instruction.value.pointer_offset.index = index_value;
        offset_instruction.value.pointer_offset.element_size = element_size;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &offset_instruction, address_id)) {
            (void)fprintf(stderr,
                          "CORE_SUBSCRIPT_STAGE function=%s stage=append-offset "
                          "base=%" PRIu32 " index=%" PRIu32 " element_size=%zu\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          base_value,
                          index_value,
                          element_size);
            return MINIC_CORE_LOWER_ERROR;
        }
        return MINIC_CORE_LOWER_OK;
    }
    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreValueId base_id;
        MinicType base_value_type;
        MinicType record_type;

        base = minic_c0_program_expression(context->body->program, expression->value.member.base);
        record =
            minic_c0_program_record(context->body->program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        /* M94_MEMBER_BASE_VALUE_TYPE: selecting a pointer member through
           `const struct *` qualifies the member lvalue storage, while evaluating
           it yields the unqualified pointer value. Nested member addressing must
           compare against that scalar value type, not the qualified lvalue type. */
        if (base == NULL || record == NULL || field == NULL || field->is_bit_field ||
            !core_scalar_expression_value_type(context->body, base, &base_value_type) ||
            !minic_type_is_pointer(base_value_type) ||
            !minic_type_pointee(base_value_type, &record_type) ||
            !minic_type_is_record(record_type) ||
            record_type.record_id != expression->value.member.record_id) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.member.base, &base_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (base_id >= context->function->value_count ||
            !minic_type_equal(context->function->values[base_id].type, base_value_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_lower_internal_append_field_address(context,
                                    expression->span,
                                    base_id,
                                    expression->value.member.record_id,
                                    expression->value.member.field_index,
                                    expression->type,
                                    address_id);
    }
    return MINIC_CORE_LOWER_UNSUPPORTED;
}

MinicCoreLowerStatus minic_core_lower_internal_append_integer_conversion(MinicCoreLowerContext *context,
                                                      MinicSourceSpan span,
                                                      MinicType target_type,
                                                      MinicCoreValueId source_value,
                                                      MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;
    const MinicCoreValue *source;

    if (context == NULL || context->function == NULL || value_id == NULL ||
        source_value >= context->function->value_count || !minic_type_is_integer(target_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source = &context->function->values[source_value];
    if (!minic_type_is_integer(source->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (minic_type_equal(source->type, target_type)) {
        *value_id = source_value;
        return MINIC_CORE_LOWER_OK;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION;
    instruction.span = span;
    instruction.type = target_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.operand = source_value;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus append_scalar_bitcast(MinicCoreLowerContext *context,
                                                  MinicSourceSpan span,
                                                  MinicType target_type,
                                                  MinicCoreValueId source_value,
                                                  MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;
    const MinicCoreValue *source;

    if (context == NULL || context->function == NULL || value_id == NULL ||
        source_value >= context->function->value_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source = &context->function->values[source_value];
    if (minic_type_equal(source->type, target_type)) {
        *value_id = source_value;
        return MINIC_CORE_LOWER_OK;
    }
    if (!minic_core_scalar_bitcast_types_valid(target_type, source->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_BITCAST;
    instruction.span = span;
    instruction.type = target_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.operand = source_value;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_internal_spill_scalar_value(MinicCoreLowerContext *context,
                                               MinicSourceSpan span,
                                               MinicType type,
                                               MinicCoreValueId value_id,
                                               MinicCoreObjectId *object_id) {
    MinicCoreInstruction instruction;
    MinicCoreValueId address_id;
    MinicType pointer_type;

    if (context == NULL || context->function == NULL || object_id == NULL ||
        !core_memory_scalar_type(type) || minic_type_is_const(type) ||
        minic_type_is_volatile(type) || value_id >= context->function->value_count ||
        !minic_type_equal(context->function->values[value_id].type, type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_core_function_add_object(context->function, span, type, object_id) ||
        !minic_type_pointer_to(type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = *object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &address_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address_id;
    instruction.value.store.stored_value = value_id;
    instruction.value.store.is_volatile = false;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_internal_store_scalar_value(MinicCoreLowerContext *context,
                                               MinicSourceSpan span,
                                               MinicType type,
                                               MinicCoreObjectId object_id,
                                               MinicCoreValueId value_id) {
    MinicCoreInstruction instruction;
    MinicCoreValueId address_id;
    MinicType pointer_type;

    if (context == NULL || context->function == NULL || !core_memory_scalar_type(type) ||
        minic_type_is_const(type) || minic_type_is_volatile(type) ||
        object_id >= context->function->object_count ||
        value_id >= context->function->value_count ||
        !minic_type_equal(context->function->objects[object_id].type, type) ||
        !minic_type_equal(context->function->values[value_id].type, type) ||
        !minic_type_pointer_to(type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &address_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address_id;
    instruction.value.store.stored_value = value_id;
    instruction.value.store.is_volatile = false;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_internal_reload_scalar_value(MinicCoreLowerContext *context,
                                                MinicSourceSpan span,
                                                MinicType type,
                                                MinicCoreObjectId object_id,
                                                MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;
    MinicCoreValueId address_id;
    MinicType pointer_type;

    if (context == NULL || context->function == NULL || value_id == NULL ||
        !core_memory_scalar_type(type) || minic_type_is_const(type) ||
        minic_type_is_volatile(type) || object_id >= context->function->object_count ||
        !minic_type_equal(context->function->objects[object_id].type, type) ||
        !minic_type_pointer_to(type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &address_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
    instruction.span = span;
    instruction.type = type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.load.address = address_id;
    instruction.value.load.is_volatile = false;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_internal_lower_scalar_equality_operands(MinicCoreLowerContext *context,
                                                           MinicExpressionId left_id,
                                                           MinicExpressionId right_id,
                                                           MinicCoreValueId *left_value,
                                                           MinicCoreValueId *right_value) {
    const MinicExpression *left_expression;
    const MinicExpression *right_expression;
    MinicCoreObjectId left_object;
    MinicCoreValueId left_normalized;
    MinicCoreValueId left_source;
    MinicCoreValueId right_normalized;
    MinicCoreValueId right_source;
    MinicCoreLowerStatus status;
    MinicType comparison_type;
    MinicType left_type;
    MinicType right_type;
    bool integer_comparison;
    bool pointer_comparison;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || left_value == NULL || right_value == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    left_expression = minic_c0_program_expression(context->body->program, left_id);
    right_expression = minic_c0_program_expression(context->body->program, right_id);
    if (left_expression == NULL || right_expression == NULL ||
        !core_scalar_expression_value_type(context->body, left_expression, &left_type) ||
        !core_scalar_expression_value_type(context->body, right_expression, &right_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    integer_comparison = false;
    pointer_comparison = false;
    if (minic_type_is_integer(left_type) && minic_type_is_integer(right_type)) {
        if (context->target == NULL ||
            !minic_target_info_integer_common_for_program(
                context->target, context->body->program, left_type, right_type, &comparison_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        integer_comparison = true;
    } else if (minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type)) {
        /* BATCH_T_FRONTEND_OWNED_POINTER_EQUALITY: legality belongs to the
           source-language semantic layer.  In particular GNU C accepts the
           established function-pointer <-> void-pointer equality extension.
           Once frontend semantics accept the expression, Core only needs one
           common pointer representation for SCALAR_EQUAL.  Prefer the normal
           C conditional common pointer type; when the GNU extension has no C
           common type, use the left representation and bitcast both operands. */
        if (!minic_c0_pointer_equality_compatible(
                context->body->program, left_id, right_id)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_type_conditional_pointer_common(
                left_type, right_type, &comparison_type)) {
            comparison_type = left_type;
        }
        pointer_comparison = true;
    } else if (minic_type_is_pointer(left_type) && minic_type_is_integer(right_type) &&
               minic_c0_expression_is_null_pointer_constant_v0(context->body->program, right_id)) {
        comparison_type = left_type;
        pointer_comparison = true;
    } else if (minic_type_is_integer(left_type) && minic_type_is_pointer(right_type) &&
               minic_c0_expression_is_null_pointer_constant_v0(context->body->program, left_id)) {
        comparison_type = right_type;
        pointer_comparison = true;
    } else {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = lower_expression(context, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (left_source >= context->function->value_count ||
        !minic_type_equal(context->function->values[left_source].type, left_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (integer_comparison) {
        status = minic_core_lower_internal_append_integer_conversion(
            context, left_expression->span, comparison_type, left_source, &left_normalized);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else if (pointer_comparison) {
        status = append_scalar_bitcast(
            context, left_expression->span, comparison_type, left_source, &left_normalized);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_spill_scalar_value(
        context, left_expression->span, comparison_type, left_normalized, &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    status = lower_expression(context, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (right_source >= context->function->value_count ||
        !minic_type_equal(context->function->values[right_source].type, right_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (integer_comparison) {
        status = minic_core_lower_internal_append_integer_conversion(
            context, right_expression->span, comparison_type, right_source, &right_normalized);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else if (pointer_comparison) {
        status = append_scalar_bitcast(
            context, right_expression->span, comparison_type, right_source, &right_normalized);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_reload_scalar_value(
        context, left_expression->span, comparison_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    *right_value = right_normalized;
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_internal_lower_integer_binary_operands(MinicCoreLowerContext *context,
                                                          MinicExpressionId left_id,
                                                          MinicExpressionId right_id,
                                                          MinicType result_type,
                                                          MinicCoreValueId *left_value,
                                                          MinicCoreValueId *right_value) {
    const MinicExpression *left_expression;
    const MinicExpression *right_expression;
    MinicCoreObjectId left_object;
    MinicCoreValueId left_normalized;
    MinicCoreValueId left_source;
    MinicCoreValueId right_normalized;
    MinicCoreValueId right_source;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || left_value == NULL || right_value == NULL ||
        !minic_type_is_integer(result_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    left_expression = minic_c0_program_expression(context->body->program, left_id);
    right_expression = minic_c0_program_expression(context->body->program, right_id);
    if (left_expression == NULL || right_expression == NULL ||
        !minic_type_is_integer(left_expression->type) ||
        !minic_type_is_integer(right_expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = lower_expression(context, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-lower status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    status = minic_core_lower_internal_append_integer_conversion(
        context, left_expression->span, result_type, left_source, &left_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-convert status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    status = minic_core_lower_internal_spill_scalar_value(
        context, left_expression->span, result_type, left_normalized, &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-spill status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }

    status = lower_expression(context, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=right-lower status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    status = minic_core_lower_internal_append_integer_conversion(
        context, right_expression->span, result_type, right_source, &right_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=right-convert status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    status =
        minic_core_lower_internal_reload_scalar_value(context, left_expression->span, result_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-reload status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    *right_value = right_normalized;
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_internal_lower_floating_binary_operands(MinicCoreLowerContext *context,
                                                         MinicExpressionId left_id,
                                                         MinicExpressionId right_id,
                                                         MinicType result_type,
                                                         MinicCoreValueId *left_value,
                                                         MinicCoreValueId *right_value) {
    const MinicExpression *left_expression;
    const MinicExpression *right_expression;
    MinicCoreObjectId left_object;
    MinicCoreValueId left_source;
    MinicCoreValueId right_source;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || left_value == NULL || right_value == NULL ||
        (!minic_type_is_float(result_type) && !minic_type_is_double(result_type) &&
         !minic_type_is_long_double(result_type))) {
        return MINIC_CORE_LOWER_ERROR;
    }
    left_expression = minic_c0_program_expression(context->body->program, left_id);
    right_expression = minic_c0_program_expression(context->body->program, right_id);
    if (left_expression == NULL || right_expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    status = minic_core_lower_internal_lower_scalar_assignment_value(context, result_type, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = minic_core_lower_internal_spill_scalar_value(
        context, left_expression->span, result_type, left_source, &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = minic_core_lower_internal_lower_scalar_assignment_value(context, result_type, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = minic_core_lower_internal_reload_scalar_value(
        context, left_expression->span, result_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (right_source >= context->function->value_count ||
        !minic_type_equal(context->function->values[right_source].type, result_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    *right_value = right_source;
    return MINIC_CORE_LOWER_OK;
}

/* BATCH_B_RECORD_COMPOUND_LITERAL_OBJECT: a block-scope record compound
   literal already owns one frontend local backing object and one initializer
   block. Materialize that exact semantic object so all aggregate consumers
   (copy, call, return) share one ownership seam. */
static MinicCoreLowerStatus lower_record_compound_literal_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *object_id) {
    const MinicBlock *initializer_block;
    MinicCoreLowerStatus status;
    bool terminated;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        expression == NULL || object_id == NULL ||
        expression->kind != MINIC_EXPRESSION_COMPOUND_LITERAL ||
        !minic_type_is_record(expression->type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    initializer_block = minic_c0_program_block(
        context->body->program, expression->value.compound_literal.initializer_block);
    if (initializer_block == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_lower_block(context, initializer_block, &terminated);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (terminated) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_local_object(
        context, expression->value.compound_literal.local_id, object_id);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (*object_id >= context->function->object_count ||
        !minic_type_equal(context->function->objects[*object_id].type, expression->type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    return MINIC_CORE_LOWER_OK;
}

/* M80_ADDRESS_BACKED_RECORD_COPY: aggregate values stay address-backed in
   Core. Resolve the subset whose storage already exists: record lvalues,
   lvalue-read wrappers, and GNU statement expressions whose final record value
   is itself address-backed. Calls/conditionals remain fail-closed. */
static MinicCoreLowerStatus lower_record_value_address(MinicCoreLowerContext *context,
                                                       MinicExpressionId expression_id,
                                                       MinicCoreValueId *address_id) {
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        address_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_record(expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* M109_CHAINED_RECORD_ASSIGNMENT_VALUE: an aggregate assignment is an
       rvalue whose bytes are the fully evaluated RHS. Keep that value
       address-backed: snapshot the RHS before evaluating the destination, copy
       the snapshot to the destination, and return the snapshot address. This
       composes chained assignments without aggregate SSA or target ABI rules. */
    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT) {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicCoreInstruction operation;
        MinicCoreObjectId snapshot_object;
        MinicCoreValueId destination_address;
        MinicCoreValueId snapshot_address;
        MinicCoreValueId source_address;
        MinicCoreLowerStatus status;
        MinicType expression_type;
        MinicType pointer_type;
        MinicType source_type;
        MinicType target_type;

        target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        source = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (target == NULL || source == NULL ||
            target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||
            minic_type_is_const(target->type) || minic_type_is_volatile(target->type) ||
            minic_type_is_volatile(source->type) ||
            !minic_type_unqualified(expression->type, &expression_type) ||
            !minic_type_unqualified(target->type, &target_type) ||
            !minic_type_unqualified(source->type, &source_type) ||
            !minic_type_equal(expression_type, target_type) ||
            !minic_type_equal(expression_type, source_type) ||
            !minic_type_is_record(expression_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        /* M127_CHAINED_RECORD_MATERIALIZED_RHS: the value of a record
           assignment is the fully evaluated RHS snapshot.  Route the RHS
           through the materializing aggregate seam so direct record-returning
           calls, record conditionals and compound literals compose with
           chained assignment exactly like ordinary address-backed records. */
        status = minic_core_lower_internal_record_materialized_address(
            context, expression->value.binary.right, &source_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, expression_type, &snapshot_object) ||
            !minic_type_pointer_to(expression_type, &pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        operation.span = expression->span;
        operation.type = pointer_type;
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.object_id = snapshot_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &operation, &snapshot_address)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
        operation.span = expression->span;
        operation.type = expression_type;
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.record_copy.destination_address = snapshot_address;
        operation.value.record_copy.source_address = source_address;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &operation)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_address(
            context, expression->value.binary.left, &destination_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        operation.span = expression->span;
        operation.type = pointer_type;
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.object_id = snapshot_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &operation, &snapshot_address)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
        operation.span = expression->span;
        operation.type = expression_type;
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.record_copy.destination_address = destination_address;
        operation.value.record_copy.source_address = snapshot_address;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &operation)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *address_id = snapshot_address;
        return MINIC_CORE_LOWER_OK;
    }
    if (!minic_c0_record_value_is_address_backed(
            context->body->program, expression_id)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* M88_RECORD_COMPOUND_LITERAL_ADDRESS: expose the shared semantic backing
       object through the address-backed aggregate seam. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL) {
        MinicCoreInstruction address_instruction;
        MinicCoreObjectId object_id;
        MinicCoreLowerStatus status;
        MinicType pointer_type;

        status = lower_record_compound_literal_object(context, expression, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (!minic_type_pointer_to(expression->type, &pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&address_instruction, 0, sizeof(address_instruction));
        address_instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        address_instruction.span = expression->span;
        address_instruction.type = pointer_type;
        address_instruction.result = MINIC_CORE_VALUE_INVALID;
        address_instruction.value.object_id = object_id;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &address_instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value_category == MINIC_VALUE_LVALUE) {
        return lower_address(context, expression_id, address_id);
    }
    if (expression->value_category != MINIC_VALUE_RVALUE) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        const MinicExpression *operand = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (operand == NULL || !minic_type_is_record(operand->type) ||
            operand->type.record_id != expression->type.record_id) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return lower_record_value_address(context, expression->value.unary.operand, address_id);
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
        const MinicBlock *block;
        const MinicExpression *result;
        MinicCoreLowerStatus status;
        bool terminated;

        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        block = minic_c0_program_block(
            context->body->program, expression->value.statement_expression.block);
        result = minic_c0_program_expression(
            context->body->program, expression->value.statement_expression.result);
        if (block == NULL || result == NULL || !minic_type_is_record(result->type) ||
            result->type.record_id != expression->type.record_id) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = minic_core_lower_internal_lower_block(context, block, &terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        /* A record-valued GNU statement expression may end in a
           materialized aggregate producer such as a conditional or compound
           literal. The statement-expression owner has already sequenced the
           block; hand its result to the aggregate materialization owner rather
           than requiring the result itself to be pre-address-backed. */
        return minic_core_lower_internal_record_materialized_address(
            context, expression->value.statement_expression.result, address_id);
    }
    return MINIC_CORE_LOWER_UNSUPPORTED;
}

/* M114_RECORD_CONDITIONAL_OBJECT: record values remain address-backed in Core.
   Materialize one private result object and copy exactly the selected arm into
   it. Arms may be ordinary address-backed records, compound literals, direct
   record-returning calls, or nested record conditionals. */
static MinicCoreLowerStatus lower_record_va_arg_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *object_id) {
    const MinicExpression *target;
    MinicCoreInstruction operation;
    MinicCoreLowerStatus status;
    MinicCoreValueId list_address;
    MinicCoreValueId cursor_value;
    MinicCoreValueId source_address;
    MinicCoreValueId destination_address;
    MinicCoreValueId one;
    MinicCoreValueId next_cursor;
    MinicType cursor_type;
    MinicType record_type;
    MinicType record_pointer_type;
    size_t value_size;
    size_t value_alignment;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || object_id == NULL ||
        expression->kind != MINIC_EXPRESSION_BUILTIN_VA_ARG ||
        !minic_type_unqualified(expression->type, &record_type) ||
        !minic_type_is_record(record_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    target = minic_c0_program_expression(
        context->body->program, expression->value.unary.operand);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(target->type) || minic_type_is_const(target->type) ||
        !minic_type_unqualified(target->type, &cursor_type) ||
        !minic_type_is_pointer(cursor_type) ||
        !minic_data_layout_type(core_data_layout(context),
                                context->body->program,
                                record_type,
                                &value_size,
                                &value_alignment) ||
        value_size == 0U || value_size > 8U || value_alignment == 0U ||
        value_alignment > 8U ||
        !minic_type_pointer_to(record_type, &record_pointer_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = lower_address(context, expression->value.unary.operand, &list_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_LOAD;
    operation.span = expression->span;
    operation.type = cursor_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.load.address = list_address;
    operation.value.load.is_volatile = minic_type_is_volatile(target->type);
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &cursor_value)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    status = append_scalar_bitcast(
        context, expression->span, record_pointer_type, cursor_value, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (!minic_core_function_add_object(
            context->function, expression->span, record_type, object_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    operation.span = expression->span;
    operation.type = record_pointer_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.object_id = *object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &destination_address)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    operation.span = expression->span;
    operation.type = record_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.record_copy.destination_address = destination_address;
    operation.value.record_copy.source_address = source_address;
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &operation)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    operation.span = expression->span;
    operation.type = minic_type_int();
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.integer_value = 1;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &one)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
    operation.span = expression->span;
    operation.type = cursor_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.pointer_offset.base = cursor_value;
    operation.value.pointer_offset.index = one;
    operation.value.pointer_offset.element_size = 8U;
    operation.value.pointer_offset.subtract = false;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &next_cursor)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_STORE;
    operation.span = expression->span;
    operation.type = minic_type_void();
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.store.address = list_address;
    operation.value.store.stored_value = next_cursor;
    operation.value.store.is_volatile = minic_type_is_volatile(target->type);
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &operation)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_internal_record_materialized_address(
    MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicCoreValueId *address_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
    MinicCoreObjectId object_id;
    MinicCoreLowerStatus status;
    MinicType pointer_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || address_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_record(expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
        const MinicBlock *statement_block;
        const MinicExpression *statement_result;
        bool terminated;

        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        statement_block = minic_c0_program_block(
            context->body->program, expression->value.statement_expression.block);
        statement_result = minic_c0_program_expression(
            context->body->program, expression->value.statement_expression.result);
        if (statement_block == NULL || statement_result == NULL ||
            !minic_type_is_record(statement_result->type) ||
            statement_result->type.record_id != expression->type.record_id) {
            return MINIC_CORE_LOWER_ERROR;
        }
        terminated = false;
        status = minic_core_lower_internal_lower_block(context, statement_block, &terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return minic_core_lower_internal_record_materialized_address(
            context, expression->value.statement_expression.result, address_id);
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_ARG) {
        status = lower_record_va_arg_object(context, expression, &object_id);
    } else if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {
        status = lower_record_conditional_object(context, expression, &object_id);
    } else if (expression->kind == MINIC_EXPRESSION_CALL &&
               expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
        status = minic_core_lower_direct_record_call_object(context, expression, &object_id);
    } else if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL) {
        status = lower_record_compound_literal_object(context, expression, &object_id);
    } else {
        return lower_record_value_address(context, expression_id, address_id);
    }
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (object_id >= context->function->object_count ||
        !minic_type_pointer_to(context->function->objects[object_id].type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = expression->span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = object_id;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, address_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_record_conditional_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *result_object) {
    const MinicExpression *false_expression;
    const MinicExpression *true_expression;
    MinicCoreBlockId false_block;
    MinicCoreBlockId merge_block;
    MinicCoreBlockId true_block;
    MinicCoreInstruction operation;
    MinicCoreLowerStatus status;
    MinicCoreValueId destination_address;
    MinicCoreValueId source_address;
    MinicType false_type;
    MinicType pointer_type;
    MinicType result_type;
    MinicType true_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || result_object == NULL ||
        expression->kind != MINIC_EXPRESSION_CONDITIONAL ||
        expression->value.conditional.uses_condition_value ||
        expression->value.conditional.when_true == MINIC_EXPRESSION_INVALID ||
        expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
        !minic_type_is_record(expression->type) ||
        !minic_type_unqualified(expression->type, &result_type) ||
        !minic_type_is_record(result_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    true_expression = minic_c0_program_expression(
        context->body->program, expression->value.conditional.when_true);
    false_expression = minic_c0_program_expression(
        context->body->program, expression->value.conditional.when_false);
    if (true_expression == NULL || false_expression == NULL ||
        !minic_type_is_record(true_expression->type) ||
        !minic_type_is_record(false_expression->type) ||
        !minic_type_unqualified(true_expression->type, &true_type) ||
        !minic_type_unqualified(false_expression->type, &false_type) ||
        !minic_type_equal(result_type, true_type) ||
        !minic_type_equal(result_type, false_type) ||
        !minic_type_pointer_to(result_type, &pointer_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_object(
            context->function, expression->span, result_type, result_object) ||
        !minic_core_function_add_block(context->function, &true_block) ||
        !minic_core_function_add_block(context->function, &false_block) ||
        !minic_core_function_add_block(context->function, &merge_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_lower_condition_branch(context,
                                    expression->value.conditional.condition,
                                    expression->span,
                                    true_block,
                                    false_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = true_block;
    status = minic_core_lower_internal_record_materialized_address(
        context, expression->value.conditional.when_true, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    operation.span = true_expression->span;
    operation.type = pointer_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.object_id = *result_object;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &destination_address)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    operation.span = true_expression->span;
    operation.type = result_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.record_copy.destination_address = destination_address;
    operation.value.record_copy.source_address = source_address;
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &operation)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_set_branch(context, context->block_id, expression->span, merge_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = false_block;
    status = minic_core_lower_internal_record_materialized_address(
        context, expression->value.conditional.when_false, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    operation.span = false_expression->span;
    operation.type = pointer_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.object_id = *result_object;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &destination_address)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    operation.span = false_expression->span;
    operation.type = result_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.record_copy.destination_address = destination_address;
    operation.value.record_copy.source_address = source_address;
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &operation)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_set_branch(context, context->block_id, expression->span, merge_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = merge_block;
    return MINIC_CORE_LOWER_OK;
}

/* BATCH_M_RECORD_LOAD: turn a record value with a materializable address
   into a private Core snapshot object.  This includes ordinary address-backed
   records and aggregate assignment values whose fully evaluated RHS snapshot
   is owned by minic_core_lower_internal_record_materialized_address().  The source pointer keeps
   its qualifiers so volatile aggregate reads remain explicit at the IR boundary. */
static MinicCoreLowerStatus lower_record_load_object(MinicCoreLowerContext *context,
                                                     MinicExpressionId expression_id,
                                                     MinicCoreObjectId *object_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
    MinicCoreLowerStatus status;
    MinicCoreValueId source_address;
    MinicType expression_type;
    MinicType source_pointee;
    MinicType source_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || object_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_record(expression->type) ||
        !minic_type_unqualified(expression->type, &expression_type) ||
        !minic_type_is_record(expression_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = minic_core_lower_internal_record_materialized_address(context, expression_id, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (source_address >= context->function->value_count ||
        !minic_type_pointee(context->function->values[source_address].type, &source_pointee) ||
        !minic_type_unqualified(source_pointee, &source_type) ||
        !minic_type_equal(source_type, expression_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_object(
            context->function, expression->span, expression_type, object_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_RECORD_LOAD;
    instruction.span = expression->span;
    instruction.type = expression_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.record_load.source_address = source_address;
    instruction.value.record_load.destination_object = *object_id;
    instruction.value.record_load.is_volatile = minic_type_is_volatile(source_pointee);
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_record_copy_statement(MinicCoreLowerContext *context,
                                                        const MinicStatement *statement) {
    const MinicExpression *source;
    const MinicExpression *target;
    const MinicRecord *record;
    MinicCoreInstruction instruction;
    MinicCoreLowerStatus status;
    MinicCoreValueId destination_address;
    MinicCoreValueId source_address;
    MinicType source_type;
    MinicType target_type;
    bool direct_record_call;
    bool record_assignment_value;
    bool record_conditional_value;
    bool record_va_arg_value;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL ||
        (statement->kind != MINIC_STATEMENT_RECORD_COPY &&
         statement->kind != MINIC_STATEMENT_RECORD_INITIALIZE)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    target = minic_c0_program_expression(context->body->program, statement->target_expression);
    source = minic_c0_program_expression(context->body->program, statement->expression);
    direct_record_call =
        source != NULL && source->kind == MINIC_EXPRESSION_CALL &&
        source->value.call.function_id != MINIC_FUNCTION_INVALID;
    record_assignment_value =
        source != NULL && source->kind == MINIC_EXPRESSION_ASSIGNMENT;
    record_conditional_value =
        source != NULL && source->kind == MINIC_EXPRESSION_CONDITIONAL;
    record_va_arg_value =
        source != NULL && source->kind == MINIC_EXPRESSION_BUILTIN_VA_ARG;
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||
        target->type.record_id != source->type.record_id ||
        !minic_type_unqualified(target->type, &target_type) ||
        !minic_type_unqualified(source->type, &source_type) ||
        !minic_type_equal(target_type, source_type) || !minic_type_is_record(target_type) ||
        (statement->kind == MINIC_STATEMENT_RECORD_COPY && minic_type_is_const(target->type)) ||
        (!direct_record_call && !record_assignment_value && !record_conditional_value &&
         !record_va_arg_value &&
         (!minic_c0_record_value_is_copy_source(context->body->program, statement->expression) ||
          !minic_c0_record_value_is_address_backed(
              context->body->program, statement->expression)))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    record = minic_c0_program_record(context->body->program, target_type.record_id);
    if (record == NULL || !record->is_complete) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (direct_record_call) {
        MinicCoreObjectId source_object;
        MinicType pointer_type;

        status = minic_core_lower_direct_record_call_object(context, source, &source_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (source_object >= context->function->object_count ||
            !minic_type_equal(context->function->objects[source_object].type, source_type) ||
            !minic_type_pointer_to(source_type, &pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = source->span;
        instruction.type = pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = source_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &source_address)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    } else if (record_conditional_value || record_va_arg_value) {
        /* Conditional records and record-valued va_arg are materialized
           aggregate producers, not pre-existing address-backed objects. */
        status = minic_core_lower_internal_record_materialized_address(
            context, statement->expression, &source_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        status = lower_record_value_address(context, statement->expression, &source_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    status = lower_address(context, statement->target_expression, &destination_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    instruction.span = statement->span;
    instruction.type = target_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.record_copy.destination_address = destination_address;
    instruction.value.record_copy.source_address = source_address;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_integer_assignment_value(MinicCoreLowerContext *context,
                                                           MinicType target_type,
                                                           MinicExpressionId expression_id,
                                                           MinicCoreValueId *value_id) {
    const MinicExpression *expression;
    MinicCoreValueId source_value;
    MinicCoreLowerStatus status;
    MinicType result_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        value_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_c0_integer_assignment_value_type(
            context->body->program, target_type, expression_id, &result_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_expression(context, expression_id, &source_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    return minic_core_lower_internal_append_integer_conversion(
        context, expression->span, result_type, source_value, value_id);
}

static MinicCoreLowerStatus append_runtime_scalar_call(
    MinicCoreLowerContext *context,
    MinicSourceSpan span,
    const char *name,
    MinicType return_type,
    const MinicType *parameter_types,
    const MinicCoreValueId *argument_values,
    size_t argument_count,
    MinicCoreValueId *value_id) {
    MinicCoreCallArgument arguments[2];
    MinicCoreCalleeId callee_id;
    MinicCoreInstruction instruction;
    size_t argument_begin;
    size_t index;

    if (context == NULL || context->function == NULL || name == NULL ||
        value_id == NULL || argument_count > 2U ||
        (argument_count != 0U && (parameter_types == NULL || argument_values == NULL))) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(arguments, 0, sizeof(arguments));
    for (index = 0U; index < argument_count; ++index) {
        if (argument_values[index] >= context->function->value_count ||
            !minic_type_equal(context->function->values[argument_values[index]].type,
                              parameter_types[index])) {
            return MINIC_CORE_LOWER_ERROR;
        }
        arguments[index].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
        arguments[index].value.value_id = argument_values[index];
    }
    if (!minic_core_function_add_callee(context->function,
                                        name,
                                        strlen(name),
                                        return_type,
                                        parameter_types,
                                        argument_count,
                                        false,
                                        &callee_id) ||
        !minic_core_function_append_call_arguments(
            context->function, arguments, argument_count, &argument_begin)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_CALL;
    instruction.span = span;
    instruction.type = return_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.call.callee_id = callee_id;
    instruction.value.call.argument_begin = argument_begin;
    instruction.value.call.argument_count = argument_count;
    instruction.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus append_long_double_conversion(
    MinicCoreLowerContext *context,
    MinicSourceSpan span,
    MinicType target_type,
    MinicType source_type,
    MinicCoreValueId source_value,
    MinicCoreValueId *value_id) {
    const char *helper;
    MinicType parameter_type;
    size_t source_alignment;
    size_t source_size;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || value_id == NULL ||
        source_value >= context->function->value_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (minic_type_equal(target_type, source_type)) {
        *value_id = source_value;
        return MINIC_CORE_LOWER_OK;
    }
    helper = NULL;
    if (minic_type_is_long_double(target_type)) {
        if (minic_type_is_double(source_type)) {
            helper = "__extenddftf2";
        } else if (minic_type_is_float(source_type)) {
            helper = "__extendsftf2";
        } else if (minic_type_is_integer(source_type) &&
                   minic_data_layout_type(core_data_layout(context),
                                          context->body->program,
                                          source_type,
                                          &source_size,
                                          &source_alignment) &&
                   source_size != 0U && source_size <= 8U) {
            (void)source_alignment;
            if (source_size <= 4U) {
                helper = minic_type_is_unsigned_integer(source_type)
                             ? "__floatunsitf"
                             : "__floatsitf";
            } else {
                helper = minic_type_is_unsigned_integer(source_type)
                             ? "__floatunditf"
                             : "__floatditf";
            }
        }
    } else if (minic_type_is_long_double(source_type)) {
        if (minic_type_is_double(target_type)) {
            helper = "__trunctfdf2";
        } else if (minic_type_is_float(target_type)) {
            helper = "__trunctfsf2";
        } else if (minic_type_is_integer(target_type)) {
            MinicCoreLowerStatus status;
            MinicCoreValueId converted_value;
            MinicType helper_return_type;
            size_t target_alignment;
            size_t target_size;

            if (!minic_data_layout_type(core_data_layout(context),
                                        context->body->program,
                                        target_type,
                                        &target_size,
                                        &target_alignment) ||
                target_size == 0U || target_size > 8U) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            (void)target_alignment;
            if (target_size <= 4U) {
                helper = minic_type_is_unsigned_integer(target_type)
                             ? "__fixunstfsi"
                             : "__fixtfsi";
                helper_return_type = minic_type_is_unsigned_integer(target_type)
                                         ? minic_type_unsigned_int()
                                         : minic_type_int();
            } else {
                helper = minic_type_is_unsigned_integer(target_type)
                             ? "__fixunstfdi"
                             : "__fixtfdi";
                helper_return_type = minic_type_is_unsigned_integer(target_type)
                                         ? minic_type_unsigned_long_long()
                                         : minic_type_long_long();
            }
            parameter_type = source_type;
            status = append_runtime_scalar_call(context,
                                                span,
                                                helper,
                                                helper_return_type,
                                                &parameter_type,
                                                &source_value,
                                                1U,
                                                &converted_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (minic_type_equal(helper_return_type, target_type)) {
                *value_id = converted_value;
                return MINIC_CORE_LOWER_OK;
            }
            return minic_core_lower_internal_append_integer_conversion(
                context, span, target_type, converted_value, value_id);
        }
    }
    if (helper == NULL) return MINIC_CORE_LOWER_UNSUPPORTED;
    parameter_type = source_type;
    return append_runtime_scalar_call(
        context, span, helper, target_type, &parameter_type, &source_value, 1U, value_id);
}

MinicCoreLowerStatus minic_core_lower_internal_append_long_double_binary_helper(
    MinicCoreLowerContext *context,
    MinicSourceSpan span,
    const char *helper,
    MinicCoreValueId left,
    MinicCoreValueId right,
    MinicType return_type,
    MinicCoreValueId *value_id) {
    MinicType parameter_types[2];
    MinicCoreValueId argument_values[2];

    parameter_types[0] = minic_type_long_double();
    parameter_types[1] = minic_type_long_double();
    argument_values[0] = left;
    argument_values[1] = right;
    return append_runtime_scalar_call(context, span, helper, return_type,
                                      parameter_types, argument_values, 2U, value_id);
}

MinicCoreLowerStatus minic_core_lower_internal_lower_scalar_assignment_value(MinicCoreLowerContext *context,
                                                          MinicType target_type,
                                                          MinicExpressionId expression_id,
                                                          MinicCoreValueId *value_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
    MinicCoreLowerStatus status;
    MinicCoreValueId source_value;
    MinicCoreValueId zero_test;
    MinicCoreValueId truth_value;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || value_id == NULL || !core_memory_scalar_type(target_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* Parser assignment compatibility intentionally stays strict so it can
       materialize explicit conversion nodes.  Core also uses this helper for
       usual-arithmetic and fixed-argument normalization, where an integer
       operand is semantically converted to binary64 without changing the
       frontend AST contract. */
    if (!minic_c0_assignment_compatible(context->body->program, target_type, expression_id) &&
        !(minic_type_is_double(target_type) && minic_type_is_integer(expression->type)) &&
        !(minic_type_is_long_double(target_type) &&
          (minic_type_is_integer(expression->type) ||
           minic_type_is_float(expression->type) ||
           minic_type_is_double(expression->type) ||
           minic_type_is_long_double(expression->type))) &&
        !((minic_type_is_float(target_type) || minic_type_is_double(target_type)) &&
          minic_type_is_long_double(expression->type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (minic_type_is_integer(target_type) && minic_type_is_integer(expression->type)) {
        return lower_integer_assignment_value(context, target_type, expression_id, value_id);
    }

    status = lower_expression(context, expression_id, &source_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (source_value >= context->function->value_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (minic_type_is_long_double(target_type)) {
        MinicType source_type;

        if (!core_scalar_expression_value_type(context->body, expression, &source_type) ||
            !minic_type_equal(context->function->values[source_value].type, source_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return append_long_double_conversion(
            context, expression->span, target_type, source_type, source_value, value_id);
    }
    if (minic_type_is_double(target_type)) {
        MinicType source_type;

        if (!core_scalar_expression_value_type(context->body, expression, &source_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_type_equal(context->function->values[source_value].type, source_type)) {
            if (getenv("CORE_FAST_TRACE") != NULL) {
                const MinicType actual_type = context->function->values[source_value].type;
                (void)fprintf(stderr,
                              "CORE_SCALAR_ASSIGN_TRACE function=%s stage=double-source-type "
                              "expr_kind=%d value=%u expected_base=%d expected_ptr=%u "
                              "actual_base=%d actual_ptr=%u expected_qual=%u actual_qual=%u\n",
                              context->source_function != NULL ? context->source_function->name : "?",
                              (int)expression->kind,
                              (unsigned int)source_value,
                              (int)source_type.base_kind,
                              source_type.pointer_depth,
                              (int)actual_type.base_kind,
                              actual_type.pointer_depth,
                              source_type.base_qualifiers,
                              actual_type.base_qualifiers);
            }
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_double(source_type)) {
            if (!minic_type_equal(source_type, target_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            *value_id = source_value;
            return MINIC_CORE_LOWER_OK;
        }
        if (minic_type_is_long_double(source_type)) {
            return append_long_double_conversion(
                context, expression->span, target_type, source_type, source_value, value_id);
        }
        if (minic_type_is_integer(source_type)) {
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE;
            instruction.span = expression->span;
            instruction.type = target_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.operand = source_value;
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (minic_type_is_float(target_type)) {
        MinicType source_type;

        if (!core_scalar_expression_value_type(context->body, expression, &source_type) ||
            !minic_type_equal(context->function->values[source_value].type, source_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (minic_type_is_long_double(source_type)) {
            return append_long_double_conversion(
                context, expression->span, target_type, source_type, source_value, value_id);
        }
        if (!minic_type_is_float(source_type) ||
            !minic_type_equal(source_type, target_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        *value_id = source_value;
        return MINIC_CORE_LOWER_OK;
    }
    if (minic_type_is_pointer(target_type)) {
        if (!minic_type_is_pointer(expression->type) &&
            !minic_c0_expression_is_null_pointer_constant_v0(context->body->program,
                                                             expression_id)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return append_scalar_bitcast(
            context, expression->span, target_type, source_value, value_id);
    }
    if (!minic_type_is_bool_integer(target_type) || !minic_type_is_pointer(expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
    instruction.span = expression->span;
    instruction.type = minic_type_int();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.operand = source_value;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &zero_test)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    instruction.value.operand = zero_test;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &truth_value)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    return minic_core_lower_internal_append_integer_conversion(context, expression->span, target_type, truth_value, value_id);
}

/* Specialized bit-field cases are matched operations; a nonmatch
 * leaves Core untouched and resumes the general expression dispatcher. */
/* Expression operation families live in dedicated Core translation units;
 * their private contract is in core_lower_internal.h. */
/* conditional pruning, void conditionals, omitted-middle GNU ternary, scalar CFG joins. All candidate branches preserve original source order. */
/* comma sequencing and short-circuit logical materialization. All candidate branches preserve original source order. */
/* GNU integer leaf builtins, bit helpers and capability-gated scalar intrinsics. All candidate branches preserve original source order. */
/* floating/integer equality and ordering, target-aware conversions. All candidate branches preserve original source order. */
/* pointer difference and pointer/integer offset semantics. All candidate branches preserve original source order. */
MinicCoreLowerStatus lower_expression(MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicCoreValueId *value_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || value_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    /* SQLite relaxed atomic read is distinct from ordinary C lvalue load. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_ATOMIC_LOAD) {
        const MinicExpression *address_expression;
        MinicCoreValueId address;
        MinicCoreInstruction atomic;
        MinicType pointee, result_type;
        MinicCoreLowerStatus status;
        address_expression = minic_c0_program_expression(
            context->body->program, expression->value.atomic_load.address);
        if (address_expression == NULL ||
            !minic_type_pointee(address_expression->type, &pointee) ||
            !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) ||
            expression->value.atomic_load.memory_order != 0U ||
            !minic_type_unqualified(pointee, &result_type) ||
            !minic_type_equal(result_type, expression->type))
            return MINIC_CORE_LOWER_UNSUPPORTED;
        status = lower_expression(context, expression->value.atomic_load.address, &address);
        if (status != MINIC_CORE_LOWER_OK) return status;
        (void)memset(&atomic, 0, sizeof(atomic));
        atomic.kind = MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
        atomic.span = expression->span;
        atomic.type = result_type;
        atomic.result = MINIC_CORE_VALUE_INVALID;
        atomic.value.atomic_load.address = address;
        atomic.value.atomic_load.memory_order = 0U;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &atomic, value_id)
                   ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
        const MinicExpression *fact_target;

        fact_target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        if (fact_target != NULL && fact_target->kind == MINIC_EXPRESSION_LOCAL) {
            if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
                (void)fprintf(stderr,
                              "LOCAL_FACT_COMPOUND_INVALIDATE function=%s local=%zu expression=%zu\n",
                              context->source_function != NULL
                                  ? context->source_function->name
                                  : "?",
                              (size_t)fact_target->value.local_id,
                              (size_t)expression_id);
            }
            minic_core_local_fact_invalidate(context, fact_target->value.local_id);
        }
    }
    /* Runtime allocation is a pointer-producing rvalue. Its target-neutral
       address value is already owned by lower_address(); admit it here so
       casts, assignments, call arguments, and other scalar consumers can
       compose with __builtin_alloca instead of failing before Core emission. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_ALLOCA) {
        return lower_address(context, expression_id, value_id);
    }
    /* M104_FUNCTION_DESIGNATOR_ADDRESS: the normalized frontend represents a
       function designator as its function-pointer semantic value already. C's
       `&function` therefore has the same pointer type and symbol identity; do
       not route it through the object-lvalue address seam. */
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        const MinicExpression *operand = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        MinicType function_type;

        if (operand != NULL && operand->kind == MINIC_EXPRESSION_FUNCTION) {
            if (!minic_type_equal(expression->type, operand->type) ||
                !minic_type_pointee(operand->type, &function_type) ||
                !minic_type_is_function(function_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            return lower_expression(context, expression->value.unary.operand, value_id);
        }
    }
    /* Bit-field scalar load follows an isolated Core lowering contract. */
    {
        bool matched = false;
        MinicCoreLowerStatus status =
            minic_core_lower_try_bit_field_read(context, expression, value_id, &matched);
        if (matched || status != MINIC_CORE_LOWER_OK) return status;
    }
    /* BATCH_R_RECORD_CALL_MEMBER_VALUE: projecting a scalar field from a
       direct aggregate-returning call consumes the call's existing result
       object. Form its address, project the field, then load the scalar value.
       This is a generic aggregate-rvalue projection; indirect aggregate calls
       and bit-field projections remain fail-closed until their own seams exist. */
    if (expression->kind == MINIC_EXPRESSION_MEMBER &&
        core_memory_scalar_type(expression->type)) {
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreObjectId result_object;
        MinicCoreValueId base_address;
        MinicCoreValueId field_address;
        MinicCoreLowerStatus status;
        MinicType pointer_type;
        MinicType value_type;

        base = minic_c0_program_expression(context->body->program, expression->value.member.base);
        record = minic_c0_program_record(context->body->program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (base != NULL && base->kind == MINIC_EXPRESSION_CALL &&
            base->value.call.function_id != MINIC_FUNCTION_INVALID &&
            minic_type_is_record(base->type) &&
            base->type.record_id == expression->value.member.record_id &&
            record != NULL && field != NULL && !field->is_bit_field &&
            minic_type_unqualified(expression->type, &value_type) &&
            core_memory_scalar_type(value_type)) {
            status = minic_core_lower_direct_record_call_object(context, base, &result_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (!minic_type_pointer_to(base->type, &pointer_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
            instruction.span = base->span;
            instruction.type = pointer_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.object_id = result_object;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &base_address)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = minic_core_lower_internal_append_field_address(context,
                                          expression->span,
                                          base_address,
                                          expression->value.member.record_id,
                                          expression->value.member.field_index,
                                          expression->type,
                                          &field_address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
            instruction.span = expression->span;
            instruction.type = value_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.load.address = field_address;
            instruction.value.load.is_volatile = minic_type_is_volatile(expression->type);
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }
    if (expression->value_category == MINIC_VALUE_LVALUE &&
        core_memory_scalar_type(expression->type)) {
        MinicCoreValueId address_id;
        MinicCoreLowerStatus status;

        status = lower_address(context, expression_id, &address_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
        instruction.span = expression->span;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.load.address = address_id;
        instruction.value.load.is_volatile = minic_type_is_volatile(expression->type);
        if (!minic_type_unqualified(expression->type, &instruction.type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value_category != MINIC_VALUE_RVALUE) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ &&
        core_memory_scalar_type(expression->type)) {
        const MinicExpression *operand;
        MinicType operand_value_type;
        MinicType result_type;

        operand = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (operand == NULL || operand->value_category != MINIC_VALUE_LVALUE ||
            !core_memory_scalar_type(operand->type) ||
            !minic_type_unqualified(operand->type, &operand_value_type) ||
            !minic_type_unqualified(expression->type, &result_type) ||
            !minic_type_equal(operand_value_type, result_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return lower_expression(context, expression->value.unary.operand, value_id);
    }
    /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: pointer += / -= evaluates
       the destination lvalue once, loads its current pointer value, applies a
       scaled integer offset, stores the updated pointer, and yields that value.
       Keep subtraction explicit in Core: negating an unsigned RHS before
       pointer.offset would change its width/extension semantics. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicCoreInstruction store;
        MinicCoreObjectId address_object;
        MinicCoreObjectId current_object;
        MinicCoreValueId address;
        MinicCoreValueId current;
        MinicCoreValueId index;
        MinicCoreValueId updated;
        MinicCoreLowerStatus status;
        MinicType address_type;
        MinicType expression_value_type;
        MinicType index_type;
        MinicType stored_type;
        size_t element_size;
        bool subtract;

        target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        if (target == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* M76_POINTER_COMPOUND_DISPATCH: only claim compound assignments whose
           destination is actually a pointer. Integer +=/-=/&=/|=/... must
           continue to the established M51 integer compound-assignment path. */
        if (minic_type_unqualified(target->type, &stored_type) &&
            minic_type_is_pointer(stored_type)) {
            source = minic_c0_program_expression(
                context->body->program, expression->value.binary.right);
            subtract = expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT;
            if (source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(target->type) ||
                (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
                 expression->value.binary.operator_kind != MINIC_BINARY_SUBTRACT) ||
                !minic_type_unqualified(expression->type, &expression_value_type) ||
                !minic_type_equal(expression_value_type, stored_type) ||
                !core_scalar_expression_value_type(context->body, source, &index_type) ||
                !minic_type_is_integer(index_type) ||
                !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                          core_data_layout(context),
                                                          stored_type,
                                                          &element_size)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_address(context, expression->value.binary.left, &address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
            instruction.span = expression->span;
            instruction.type = stored_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.load.address = address;
            instruction.value.load.is_volatile = minic_type_is_volatile(target->type);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &current)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            /* M126A_POINTER_COMPOUND_BLOCK_LOCAL: the RHS may create CFG (for
               example a call with a conditional argument). Preserve both the
               destination address and its pre-update pointer value before
               lowering that RHS so POINTER_OFFSET and STORE are formed from
               values reloaded in the final RHS block. */
            if (address >= context->function->value_count) {
                return MINIC_CORE_LOWER_ERROR;
            }
            address_type = context->function->values[address].type;
            status = minic_core_lower_internal_spill_scalar_value(
                context, target->span, address_type, address, &address_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = minic_core_lower_internal_spill_scalar_value(
                context, target->span, stored_type, current, &current_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = lower_expression(context, expression->value.binary.right, &index);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (index >= context->function->value_count ||
                !minic_type_equal(context->function->values[index].type, index_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = minic_core_lower_internal_reload_scalar_value(
                context, target->span, stored_type, current_object, &current);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = minic_core_lower_internal_reload_scalar_value(
                context, target->span, address_type, address_object, &address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
            instruction.span = expression->span;
            instruction.type = stored_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.pointer_offset.base = current;
            instruction.value.pointer_offset.index = index;
            instruction.value.pointer_offset.element_size = element_size;
            instruction.value.pointer_offset.subtract = subtract;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &updated)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&store, 0, sizeof(store));
            store.kind = MINIC_CORE_INSTRUCTION_STORE;
            store.span = expression->span;
            store.type = minic_type_void();
            store.result = MINIC_CORE_VALUE_INVALID;
            store.value.store.address = address;
            store.value.store.stored_value = updated;
            store.value.store.is_volatile = minic_type_is_volatile(target->type);
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &store)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            *value_id = updated;
            return MINIC_CORE_LOWER_OK;
        }
    }

    /* M115_CHAINED_BIT_FIELD_ASSIGNMENT: a simple scalar assignment has one
       lowering owner whether its value is discarded by statement context or
       consumed by a surrounding expression.  Reuse lower_assignment_pair so
       addressable scalars and unsigned bit-fields share exactly the same store
       semantics; expression context additionally receives the value actually
       stored after destination conversion/bit-field truncation. */
    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT) {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicType expression_value_type;
        MinicType stored_type;

        target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        source = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (target == NULL || source == NULL ||
            target->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(target->type) ||
            !minic_type_unqualified(target->type, &stored_type) ||
            !core_memory_scalar_type(stored_type) ||
            !minic_type_unqualified(expression->type, &expression_value_type) ||
            !minic_type_equal(expression_value_type, stored_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return minic_core_lower_assignment_pair(context,
                                     expression->value.binary.left,
                                     expression->value.binary.right,
                                     expression->span,
                                     value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_DISCARD) {
        const MinicExpression *operand;
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        if (!minic_type_is_void(expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        operand =
            minic_c0_program_expression(context->body->program, expression->value.unary.operand);
        if (operand == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* M130_DISCARDED_LVALUE_EFFECT_OWNER: a non-volatile lvalue whose value
           is explicitly discarded does not need an rvalue load, but evaluating
           the lvalue can still have effects through its base/index expression.
           Ask the established address owner to perform exactly that evaluation.
           Only claim shapes that are genuinely addressable; unsupported
           bit-fields or other special lvalues continue through the old value
           path. Volatile lvalues also stay on the value path so their observable
           read is preserved. */
        if (operand->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_volatile(operand->type)) {
            status = lower_address(
                context, expression->value.unary.operand, &discarded_value);
            if (status == MINIC_CORE_LOWER_OK) {
                *value_id = MINIC_CORE_VALUE_INVALID;
                return MINIC_CORE_LOWER_OK;
            }
            if (status == MINIC_CORE_LOWER_ERROR) {
                return status;
            }
        }
        status = lower_expression(context, expression->value.unary.operand, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (minic_type_is_void(operand->type)) {
            if (discarded_value != MINIC_CORE_VALUE_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
        } else if (discarded_value == MINIC_CORE_VALUE_INVALID ||
                   discarded_value >= context->function->value_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = MINIC_CORE_VALUE_INVALID;
        return MINIC_CORE_LOWER_OK;
    }
    if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS) {
        const MinicStatement *label_statement; MinicCoreBlockId label_block; MinicCoreLowerStatus status;
        label_statement = minic_c0_program_statement(context->body->program, expression->value.label_statement_id);
        if (label_statement == NULL) return MINIC_CORE_LOWER_ERROR;
        if (label_statement->kind != MINIC_STATEMENT_LABEL || !minic_type_is_pointer(expression->type)) return MINIC_CORE_LOWER_UNSUPPORTED;
        status = ensure_statement_block(context, expression->value.label_statement_id, &label_block);
        if (status != MINIC_CORE_LOWER_OK) return status;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS; instruction.span = expression->span; instruction.type = expression->type; instruction.result = MINIC_CORE_VALUE_INVALID; instruction.value.block_id = label_block;
        return minic_core_function_append_value_instruction(context->function, context->block_id, &instruction, value_id) ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
        const MinicBlock *statement_block;
        const MinicExpression *statement_result;
        MinicCoreValueId result_value;
        MinicCoreLowerStatus status;
        MinicType result_type;
        bool terminated;

        /* M50B_EFFECT_ONLY_STATEMENT_EXPRESSION: a GNU ({ ... }) whose last
           statement has no value is an effect expression, not a scalar one. */
        statement_block = minic_c0_program_block(context->body->program,
                                                 expression->value.statement_expression.block);
        if (statement_block == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* M158_FINAL_STRICT_TAIL_VOID_STMT_EXPR: GNU macros commonly wrap an
           effect expression as `(void)({ call(); })`.  Such a statement
           expression has a real result expression id even though both that
           result and the whole expression are void.  Execute the owned block,
           then the result expression, and require that neither manufactures an
           SSA value.  Scalar statement expressions keep the existing path. */
        if (minic_type_is_void(expression->type) &&
            expression->value.statement_expression.result != MINIC_EXPRESSION_INVALID) {
            statement_result = minic_c0_program_expression(
                context->body->program, expression->value.statement_expression.result);
            if (statement_result == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!minic_type_is_void(statement_result->type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = minic_core_lower_internal_lower_block(context, statement_block, &terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (terminated) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_expression(
                context, expression->value.statement_expression.result, &result_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (result_value != MINIC_CORE_VALUE_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
            *value_id = MINIC_CORE_VALUE_INVALID;
            return MINIC_CORE_LOWER_OK;
        }
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            if (!minic_type_is_void(expression->type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = minic_core_lower_internal_lower_block(context, statement_block, &terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (terminated) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            *value_id = MINIC_CORE_VALUE_INVALID;
            return MINIC_CORE_LOWER_OK;
        }
        if (!core_scalar_expression_value_type(context->body, expression, &result_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        statement_result = minic_c0_program_expression(
            context->body->program, expression->value.statement_expression.result);
        if (statement_result == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = minic_core_lower_internal_lower_block(context, statement_block, &terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status =
            lower_expression(context, expression->value.statement_expression.result, &result_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (result_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[result_value].type, result_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = result_value;
        return MINIC_CORE_LOWER_OK;
    }
    /* Delegate conditional as one ordered, fail-closed semantic family. */
    {
        bool handled = false;
        MinicCoreLowerStatus status = minic_core_lower_try_conditional(
            context, expression, value_id, &handled);
        if (handled) return status;
    }
    /* Delegate sequenced as one ordered, fail-closed semantic family. */
    {
        bool handled = false;
        MinicCoreLowerStatus status = minic_core_lower_try_sequenced(
            context, expression, value_id, expression_id, &handled);
        if (handled) return status;
    }
    /* M81_FUNCTION_ADDRESS_VALUE: a function designator is already a
       pointer-to-function semantic value in the normalized AST. Core records
       only the symbol identity; calling through that pointer is a later seam. */
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        const MinicFunction *designator;
        const char *symbol_name;
        size_t symbol_name_length;
        MinicCoreFunctionSymbolId symbol_id;
        MinicType function_type;

        designator = minic_c0_program_function(
            context->body->program, expression->value.function_id);
        if (designator == NULL ||
            !minic_type_pointee(expression->type, &function_type) ||
            !minic_type_is_function(function_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        symbol_name = designator->assembler_name != NULL ? designator->assembler_name
                                                          : designator->name;
        symbol_name_length = designator->assembler_name != NULL
                                 ? designator->assembler_name_length
                                 : designator->name_length;
        if (symbol_name == NULL || symbol_name_length == 0U ||
            !minic_core_function_add_function_symbol(
                context->function, symbol_name, symbol_name_length, &symbol_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.function_symbol_id = symbol_id;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        const MinicExpression *addressed_expression;
        MinicCoreLowerStatus status;

        addressed_expression = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (addressed_expression != NULL &&
            addressed_expression->kind == MINIC_EXPRESSION_LOCAL) {
            minic_core_local_fact_escape(context, addressed_expression->value.local_id);
        }
        status = lower_address(context, expression->value.unary.operand, value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (*value_id >= context->function->value_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_equal(context->function->values[*value_id].type, expression->type)) {
            return MINIC_CORE_LOWER_OK;
        }
        /* M90_LEGACY_ARRAY_ADDRESS_OF: legacy local/member arrays keep their
           element type plus array-object metadata. lower_address() therefore
           yields the element-address representation, while C `&array` carries
           pointer-to-array type. The address bits are identical; preserve the
           semantic pointer type with Core's target-neutral scalar bitcast. */
        {
            const MinicExpression *addressed = minic_c0_program_expression(
                context->body->program, expression->value.unary.operand);
            MinicArrayObjectInfo array_info;

            (void)memset(&array_info, 0, sizeof(array_info));
            if (addressed != NULL &&
                minic_c0_expression_array_object_info(
                    context->body->program, addressed, &array_info) &&
                minic_type_is_pointer(context->function->values[*value_id].type) &&
                minic_type_is_pointer(expression->type)) {
                return append_scalar_bitcast(
                    context, expression->span, expression->type, *value_id, value_id);
            }
        }
        return MINIC_CORE_LOWER_ERROR;
    }
    /* Delegate builtin unary as one ordered, fail-closed semantic family. */
    {
        bool handled = false;
        MinicCoreLowerStatus status = minic_core_lower_try_builtin_unary(
            context, expression, value_id, &handled);
        if (handled) return status;
    }

    if (expression->kind == MINIC_EXPRESSION_CALL) {
        if (expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
            return minic_core_lower_indirect_call(context, expression, value_id);
        }
        return minic_core_lower_direct_call(context, expression, value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_FIXED_REGISTER) {
        const MinicFixedRegisterBinding *binding;
        size_t core_binding_id;

        binding = minic_c0_program_fixed_register_binding(
            context->body->program, expression->value.fixed_register_binding_id);
        if (binding == NULL || binding->register_name == NULL ||
            binding->register_name_length == 0U) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!core_memory_scalar_type(binding->type) ||
            !minic_type_equal(binding->type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!core_import_fixed_register_binding(context,
                                                expression->value.fixed_register_binding_id,
                                                &core_binding_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.fixed_register_binding_id = core_binding_id;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT)) {
        const MinicExpression *updated_expression = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (updated_expression != NULL &&
            updated_expression->kind == MINIC_EXPRESSION_LOCAL) {
            minic_core_local_fact_invalidate(context, updated_expression->value.local_id);
        }
        return minic_core_lower_scalar_update(context, expression, value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_PLUS &&
        (minic_type_is_float(expression->type) || minic_type_is_double(expression->type) ||
         minic_type_is_long_double(expression->type))) {
        MinicCoreLowerStatus status;
        MinicCoreValueId operand_value;

        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        *value_id = operand_value;
        return MINIC_CORE_LOWER_OK;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_NEGATE &&
        minic_type_is_long_double(expression->type)) {
        MinicCoreInstruction zero_instruction;
        MinicCoreValueId operand_value;
        MinicCoreValueId zero_value;
        MinicCoreLowerStatus status;

        status = minic_core_lower_internal_lower_scalar_assignment_value(context, minic_type_long_double(),
                                               expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) return status;
        (void)memset(&zero_instruction, 0, sizeof(zero_instruction));
        zero_instruction.kind = MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
        zero_instruction.span = expression->span;
        zero_instruction.type = minic_type_long_double();
        zero_instruction.result = MINIC_CORE_VALUE_INVALID;
        zero_instruction.value.floating128_bits.low = 0U;
        zero_instruction.value.floating128_bits.high = 0U;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &zero_instruction, &zero_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_lower_internal_append_long_double_binary_helper(
            context, expression->span, "__subtf3", zero_value, operand_value,
            minic_type_long_double(), value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_NEGATE &&
        minic_type_is_float(expression->type)) {
        const MinicExpression *operand_expression;
        MinicCoreInstruction float_instruction;
        MinicCoreLowerStatus status;
        MinicCoreValueId operand_value;
        MinicCoreValueId widened_value;
        MinicCoreValueId negated_value;

        operand_expression = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (operand_expression == NULL || !minic_type_is_float(operand_expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_is_float(context->function->values[operand_value].type)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        (void)memset(&float_instruction, 0, sizeof(instruction));
        float_instruction.kind = MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE;
        float_instruction.span = expression->span;
        float_instruction.type = minic_type_double();
        float_instruction.result = MINIC_CORE_VALUE_INVALID;
        float_instruction.value.operand = operand_value;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &float_instruction, &widened_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        float_instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE;
        float_instruction.type = minic_type_double();
        float_instruction.value.operand = widened_value;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &float_instruction, &negated_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        float_instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT;
        float_instruction.type = expression->type;
        float_instruction.value.operand = negated_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &float_instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_NEGATE &&
        minic_type_is_double(expression->type)) {
        const MinicExpression *operand_expression;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;

        operand_expression = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (operand_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = minic_core_lower_internal_lower_scalar_assignment_value(context,
                                               minic_type_double(),
                                               expression->value.unary.operand,
                                               &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE;
        instruction.span = expression->span;
        instruction.type = minic_type_double();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_NEGATE) {
        const MinicExpression *operand_expression;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;
        MinicType promoted_type;

        operand_expression = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (context->target == NULL || operand_expression == NULL ||
            !minic_type_is_integer(expression->type) ||
            !minic_type_is_integer(operand_expression->type) ||
            !minic_target_info_integer_promotion_for_program(
                context->target, context->body->program, operand_expression->type, &promoted_type) ||
            !minic_type_equal(promoted_type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_append_integer_conversion(
            context, operand_expression->span, promoted_type, operand_value, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_NEGATE;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_BITWISE_NOT) {
        const MinicExpression *operand_expression;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;
        MinicType promoted_type;

        operand_expression = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (context->target == NULL || operand_expression == NULL ||
            !minic_type_is_integer(expression->type) ||
            !minic_type_is_integer(operand_expression->type) ||
            !minic_target_info_integer_promotion_for_program(
                context->target, context->body->program, operand_expression->type, &promoted_type) ||
            !minic_type_equal(promoted_type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_append_integer_conversion(
            context, operand_expression->span, promoted_type, operand_value, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !core_memory_scalar_type(context->function->values[operand_value].type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.span = expression->span;
    instruction.type = expression->type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    if (expression->kind == MINIC_EXPRESSION_INTEGER) {
        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.value.integer_value = expression->value.integer_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_FLOATING) {
        if (!minic_type_is_float(expression->type) &&
            !minic_type_is_double(expression->type) &&
            !minic_type_is_long_double(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
        if (minic_type_is_long_double(expression->type)) {
            instruction.value.floating128_bits.low = expression->value.floating128_bits.low;
            instruction.value.floating128_bits.high = expression->value.floating128_bits.high;
        } else {
            instruction.value.floating_bits = expression->value.floating_bits;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_CONVERSION) {
        const MinicExpression *operand;
        MinicExpressionId operand_id;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;
        MinicType source_type;
        MinicType target_type;

        operand_id = expression->value.unary.operand;
        operand = minic_c0_program_expression(context->body->program, operand_id);
        if (operand == NULL ||
            !minic_type_unqualified(expression->type, &target_type) ||
            !core_scalar_expression_value_type(context->body, operand, &source_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, operand_id, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type, source_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_integer(target_type) && minic_type_is_integer(source_type)) {
            return minic_core_lower_internal_append_integer_conversion(
                context, expression->span, target_type, operand_value, value_id);
        }
        if (minic_type_is_long_double(target_type) ||
            minic_type_is_long_double(source_type)) {
            return append_long_double_conversion(
                context, expression->span, target_type, source_type, operand_value, value_id);
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.span = expression->span;
        instruction.type = target_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        if (minic_type_is_double(target_type) && minic_type_is_integer(source_type)) {
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE;
        } else if (minic_type_is_double(target_type) && minic_type_is_float(source_type)) {
            instruction.kind = MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE;
        } else if (minic_type_is_float(target_type) && minic_type_is_double(source_type)) {
            instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT;
        } else if (minic_type_is_float(target_type) && minic_type_is_integer(source_type)) {
            MinicCoreValueId widened_value;

            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE;
            instruction.type = minic_type_double();
            if (!minic_core_function_append_value_instruction(
                    context->function,
                    context->block_id,
                    &instruction,
                    &widened_value)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT;
            instruction.type = target_type;
            instruction.value.operand = widened_value;
        } else if (minic_type_is_integer(target_type) && minic_type_is_double(source_type)) {
            instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER;
        } else {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    /* GNU va_arg is a stateful cursor read.  The frontend keeps va_list as a
       modifiable pointer lvalue; Core performs the target-neutral load, typed
       dereference, one ABI-slot cursor advance, and writeback.  RV64 owns the
       concrete 8-byte variadic slot width. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_ARG) {
        const MinicExpression *target;
        MinicCoreInstruction operation;
        MinicCoreLowerStatus status;
        MinicCoreValueId list_address;
        MinicCoreValueId cursor_value;
        MinicCoreValueId value_address;
        MinicCoreValueId argument_value;
        MinicCoreValueId one;
        MinicCoreValueId next_cursor;
        MinicType cursor_type;
        MinicType value_pointer_type;
        size_t value_size;
        size_t value_alignment;

        target = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (target == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_pointer(target->type) || minic_type_is_const(target->type) ||
            !minic_type_unqualified(target->type, &cursor_type) ||
            !minic_type_is_pointer(cursor_type) ||
            (!minic_type_is_integer(expression->type) &&
             !minic_type_is_pointer(expression->type) &&
             !minic_type_is_double(expression->type)) ||
            !minic_data_layout_type(core_data_layout(context),
                                    context->body->program,
                                    expression->type,
                                    &value_size,
                                    &value_alignment) ||
            value_size == 0U || value_size > 8U || value_alignment > 8U ||
            !minic_type_pointer_to(expression->type, &value_pointer_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_address(context, expression->value.unary.operand, &list_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (context->target != NULL &&
            (context->target->architecture == MINIC_TARGET_ARCH_X86_64 ||
             context->target->architecture == MINIC_TARGET_ARCH_AARCH64)) {
            /* SysV x86 and AAPCS64 use an ABI-owned register-bank cursor,
             * unlike the linear 8-byte pointer path of RV64/LoongArch.
             * VARIADIC_NEXT_SCALAR handles the typed read and advance. */
            /* An x86 __va_list_tag tracks two register banks and overflow
             * stack storage.  Core must not model it as RV64's linear
             * 8-byte pointer increment. Emit one typed semantic read. */
            (void)memset(&operation, 0, sizeof(operation));
            operation.kind = MINIC_CORE_INSTRUCTION_LOAD;
            operation.span = expression->span;
            operation.type = cursor_type;
            operation.result = MINIC_CORE_VALUE_INVALID;
            operation.value.load.address = list_address;
            operation.value.load.is_volatile = minic_type_is_volatile(target->type);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &operation,
                    &cursor_value)) return MINIC_CORE_LOWER_ERROR;
            (void)memset(&operation, 0, sizeof(operation));
            operation.kind = MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR;
            operation.span = expression->span;
            operation.type = expression->type;
            operation.result = MINIC_CORE_VALUE_INVALID;
            operation.value.operand = cursor_value;
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &operation,
                       value_id)
                       ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_LOAD;
        operation.span = expression->span;
        operation.type = cursor_type;
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.load.address = list_address;
        operation.value.load.is_volatile = minic_type_is_volatile(target->type);
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &operation, &cursor_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = append_scalar_bitcast(
            context, expression->span, value_pointer_type, cursor_value, &value_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_LOAD;
        operation.span = expression->span;
        operation.type = expression->type;
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.load.address = value_address;
        operation.value.load.is_volatile = false;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &operation, &argument_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        operation.span = expression->span;
        operation.type = minic_type_int();
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.integer_value = 1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &operation, &one)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        operation.span = expression->span;
        operation.type = cursor_type;
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.pointer_offset.base = cursor_value;
        operation.value.pointer_offset.index = one;
        operation.value.pointer_offset.element_size = 8U;
        operation.value.pointer_offset.subtract = false;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &operation, &next_cursor)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_STORE;
        operation.span = expression->span;
        operation.type = minic_type_void();
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.store.address = list_address;
        operation.value.store.stored_value = next_cursor;
        operation.value.store.is_volatile = minic_type_is_volatile(target->type);
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &operation)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = argument_value;
        return MINIC_CORE_LOWER_OK;
    }

    /* M79_CALL_FRAME_RETURN_ADDRESS: keep the semantic builtin in Core rather
       than lowering it to a target register in the frontend. The first seam
       is GNU __builtin_return_address(0); deeper levels and frame-address
       queries remain unsupported until a backend can define them correctly. */
    if (expression->kind == MINIC_EXPRESSION_CALL_FRAME_ADDRESS) {
        MinicCoreCallFrameAddressKind core_kind;
        MinicType pointee;

        if (expression->value.call_frame_address.level != 0U ||
            !minic_type_pointee(expression->type, &pointee) || !minic_type_is_void(pointee)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        switch (expression->value.call_frame_address.kind) {
        case MINIC_CALL_FRAME_ADDRESS_RETURN:
            core_kind = MINIC_CORE_CALL_FRAME_ADDRESS_RETURN;
            break;
        case MINIC_CALL_FRAME_ADDRESS_FRAME:
            core_kind = MINIC_CORE_CALL_FRAME_ADDRESS_FRAME;
            break;
        default:
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.call_frame_address.kind = core_kind;
        instruction.value.call_frame_address.level = 0U;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BITCAST) {
        const MinicExpression *operand;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;
        MinicType operand_value_type;

        operand =
            minic_c0_program_expression(context->body->program, expression->value.unary.operand);
        if (operand == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!core_scalar_expression_value_type(context->body, operand, &operand_value_type) ||
            !minic_core_scalar_bitcast_types_valid(expression->type, operand_value_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type, operand_value_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return append_scalar_bitcast(
            context, expression->span, expression->type, operand_value, value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_OVERFLOW &&
        (expression->value.overflow.operator_kind == MINIC_OVERFLOW_ADD ||
         expression->value.overflow.operator_kind == MINIC_OVERFLOW_SUBTRACT ||
         expression->value.overflow.operator_kind == MINIC_OVERFLOW_MULTIPLY)) {
        const MinicExpression *left_expression;
        const MinicExpression *result_pointer_expression;
        const MinicExpression *right_expression;
        MinicCoreObjectId left_object;
        MinicCoreObjectId right_object;
        MinicCoreValueId left;
        MinicCoreValueId left_source;
        MinicCoreValueId result_address;
        MinicCoreValueId right;
        MinicCoreValueId right_source;
        MinicCoreLowerStatus status;
        MinicType left_type;
        MinicType left_value_type;
        MinicType result_type;
        MinicType right_type;
        MinicType right_value_type;

        if (!minic_type_equal(expression->type, minic_type_bool())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.overflow.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.overflow.right);
        result_pointer_expression = minic_c0_program_expression(
            context->body->program, expression->value.overflow.result_pointer);
        if (left_expression == NULL || right_expression == NULL ||
            result_pointer_expression == NULL || context->target == NULL ||
            !minic_type_pointee(result_pointer_expression->type, &result_type) ||
            !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||
            minic_type_is_const(result_type) || minic_type_is_volatile(result_type) ||
            !core_scalar_expression_value_type(context->body, left_expression, &left_type) ||
            !core_scalar_expression_value_type(context->body, right_expression, &right_type) ||
            !minic_type_is_integer(left_type) || !minic_type_is_integer(right_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        left_value_type =
            minic_c0_integer_range_representable_in_type(
                context->body->program, context->target, left_type, result_type)
                ? result_type
                : left_type;
        right_value_type =
            minic_c0_integer_range_representable_in_type(
                context->body->program, context->target, right_type, result_type)
                ? result_type
                : right_type;

        status = lower_expression(context, expression->value.overflow.left, &left_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (left_source >= context->function->value_count ||
            !minic_type_equal(context->function->values[left_source].type, left_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_equal(left_value_type, left_type)) {
            left = left_source;
        } else {
            status = minic_core_lower_internal_append_integer_conversion(
                context, left_expression->span, left_value_type, left_source, &left);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        status =
            minic_core_lower_internal_spill_scalar_value(context, left_expression->span, left_value_type, left, &left_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        status = lower_expression(context, expression->value.overflow.right, &right_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (right_source >= context->function->value_count ||
            !minic_type_equal(context->function->values[right_source].type, right_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_equal(right_value_type, right_type)) {
            right = right_source;
        } else {
            status = minic_core_lower_internal_append_integer_conversion(
                context, right_expression->span, right_value_type, right_source, &right);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        status =
            minic_core_lower_internal_spill_scalar_value(context, right_expression->span, right_value_type, right, &right_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        status =
            lower_expression(context, expression->value.overflow.result_pointer, &result_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            minic_core_lower_internal_reload_scalar_value(context, left_expression->span, left_value_type, left_object, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            minic_core_lower_internal_reload_scalar_value(context, right_expression->span, right_value_type, right_object, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (left >= context->function->value_count || right >= context->function->value_count ||
            result_address >= context->function->value_count ||
            !minic_type_equal(context->function->values[left].type, left_value_type) ||
            !minic_type_equal(context->function->values[right].type, right_value_type) ||
            !minic_type_equal(context->function->values[result_address].type,
                              result_pointer_expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW;
        instruction.type = minic_type_bool();
        instruction.value.integer_overflow.operator_kind =
            expression->value.overflow.operator_kind == MINIC_OVERFLOW_ADD
                ? MINIC_CORE_INTEGER_OVERFLOW_ADD
            : expression->value.overflow.operator_kind == MINIC_OVERFLOW_SUBTRACT
                ? MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT
                : MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY;
        instruction.value.integer_overflow.left = left;
        instruction.value.integer_overflow.right = right;
        instruction.value.integer_overflow.result_address = result_address;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

    /* Delegate comparisons as one ordered, fail-closed semantic family. */
    {
        bool handled = false;
        MinicCoreLowerStatus status = minic_core_lower_try_comparisons(
            context, expression, value_id, &handled);
        if (handled) return status;
    }
    /* Delegate pointer arithmetic as one ordered, fail-closed semantic family. */
    {
        bool handled = false;
        MinicCoreLowerStatus status = minic_core_lower_try_pointer_arithmetic(
            context, expression, value_id, &handled);
        if (handled) return status;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE ||
         expression->value.binary.operator_kind == MINIC_BINARY_REMAINDER ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_XOR) &&
        minic_type_is_integer(expression->type)) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;
        status = minic_core_lower_internal_lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_SUBTRACT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
            break;
        case MINIC_BINARY_MULTIPLY:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY;
            break;
        case MINIC_BINARY_DIVIDE:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE;
            break;
        case MINIC_BINARY_REMAINDER:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER;
            break;
        case MINIC_BINARY_BITWISE_XOR:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR;
            break;
        default:
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE) &&
        minic_type_is_long_double(expression->type)) {
        const char *helper;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        status = minic_core_lower_internal_lower_floating_binary_operands(
            context, expression->value.binary.left, expression->value.binary.right,
            minic_type_long_double(), &left, &right);
        if (status != MINIC_CORE_LOWER_OK) return status;
        helper = expression->value.binary.operator_kind == MINIC_BINARY_ADD ? "__addtf3" :
                 expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ? "__subtf3" :
                 expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ? "__multf3" :
                 "__divtf3";
        return minic_core_lower_internal_append_long_double_binary_helper(
            context, expression->span, helper, left, right, minic_type_long_double(), value_id);
    }

    /* RUNTIME_R0_SCALAR_FLOATING: preserve left-to-right evaluation by
       spilling the left binary32/binary64 value across RHS lowering. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE) &&
        (minic_type_is_float(expression->type) || minic_type_is_double(expression->type))) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;
        bool is_float;

        is_float = minic_type_is_float(expression->type);
        status = minic_core_lower_internal_lower_floating_binary_operands(context,
                                              expression->value.binary.left,
                                              expression->value.binary.right,
                                              expression->type,
                                              &left,
                                              &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_ADD:
            instruction.kind = is_float ? MINIC_CORE_INSTRUCTION_FLOAT_ADD
                                        : MINIC_CORE_INSTRUCTION_DOUBLE_ADD;
            break;
        case MINIC_BINARY_SUBTRACT:
            instruction.kind = is_float ? MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT
                                        : MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT;
            break;
        case MINIC_BINARY_MULTIPLY:
            instruction.kind = is_float ? MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY
                                        : MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY;
            break;
        case MINIC_BINARY_DIVIDE:
            instruction.kind = is_float ? MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE
                                        : MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE;
            break;
        default:
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = minic_core_lower_internal_lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = minic_core_lower_internal_lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = minic_core_lower_internal_lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreObjectId left_object;
        MinicCoreValueId left;
        MinicCoreValueId left_source;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !minic_type_is_integer(left_expression->type) ||
            !minic_type_is_integer(right_expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.binary.left, &left_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_append_integer_conversion(
            context, left_expression->span, expression->type, left_source, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_spill_scalar_value(
            context, left_expression->span, expression->type, left, &left_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_reload_scalar_value(
            context, left_expression->span, expression->type, left_object, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (left >= context->function->value_count || right >= context->function->value_count ||
            !minic_type_equal(context->function->values[left].type, expression->type) ||
            !minic_type_is_integer(context->function->values[right].type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.kind = expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT
                               ? MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT
                               : MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    /* Bit-field RMW keeps one-evaluation/volatile semantics intact. */
    {
        bool matched = false;
        MinicCoreLowerStatus status =
            minic_core_lower_try_bit_field_compound(context, expression, value_id, &matched);
        if (matched || status != MINIC_CORE_LOWER_OK) return status;
    }
    /* RUNTIME_R1_DOUBLE_COMPOUND_ASSIGNMENT: binary64 compound arithmetic
       reuses the same evaluate-address/load/spill/RHS/reload/store ownership
       as integer compound assignment.  Keep remainder/bitwise/shift excluded:
       C does not define those operators for floating operands. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE)) {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicCoreObjectId address_object;
        MinicCoreObjectId current_object;
        MinicCoreValueId address;
        MinicCoreValueId current;
        MinicCoreValueId right;
        MinicCoreValueId result;
        MinicCoreLowerStatus status;
        MinicType address_type;
        MinicType expression_value_type;
        MinicType stored_type;

        target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        source = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (target != NULL && source != NULL &&
            target->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(target->type) &&
            minic_type_unqualified(target->type, &stored_type) &&
            (minic_type_is_float(stored_type) || minic_type_is_double(stored_type) ||
             minic_type_is_long_double(stored_type))) {
            if (!minic_type_unqualified(expression->type, &expression_value_type) ||
                !minic_type_equal(expression_value_type, stored_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_address(context, expression->value.binary.left, &address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
            instruction.span = target->span;
            instruction.type = stored_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.load.address = address;
            instruction.value.load.is_volatile = minic_type_is_volatile(target->type);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &current) ||
                address >= context->function->value_count) {
                return MINIC_CORE_LOWER_ERROR;
            }
            address_type = context->function->values[address].type;
            status = minic_core_lower_internal_spill_scalar_value(
                context, target->span, address_type, address, &address_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = minic_core_lower_internal_spill_scalar_value(
                context, target->span, stored_type, current, &current_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = minic_core_lower_internal_lower_scalar_assignment_value(
                context, stored_type, expression->value.binary.right, &right);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = minic_core_lower_internal_reload_scalar_value(
                context, target->span, stored_type, current_object, &current);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = minic_core_lower_internal_reload_scalar_value(
                context, target->span, address_type, address_object, &address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (minic_type_is_long_double(stored_type)) {
                const char *helper =
                    expression->value.binary.operator_kind == MINIC_BINARY_ADD ? "__addtf3" :
                    expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ? "__subtf3" :
                    expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ? "__multf3" :
                    "__divtf3";
                status = minic_core_lower_internal_append_long_double_binary_helper(
                    context, expression->span, helper, current, right,
                    minic_type_long_double(), &result);
                if (status != MINIC_CORE_LOWER_OK) return status;
            } else {
                (void)memset(&instruction, 0, sizeof(instruction));
                switch (expression->value.binary.operator_kind) {
                case MINIC_BINARY_ADD:
                    instruction.kind = minic_type_is_float(stored_type)
                                           ? MINIC_CORE_INSTRUCTION_FLOAT_ADD
                                           : MINIC_CORE_INSTRUCTION_DOUBLE_ADD;
                    break;
                case MINIC_BINARY_SUBTRACT:
                    instruction.kind = minic_type_is_float(stored_type)
                                           ? MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT
                                           : MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT;
                    break;
                case MINIC_BINARY_MULTIPLY:
                    instruction.kind = minic_type_is_float(stored_type)
                                           ? MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY
                                           : MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY;
                    break;
                case MINIC_BINARY_DIVIDE:
                    instruction.kind = minic_type_is_float(stored_type)
                                           ? MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE
                                           : MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE;
                    break;
                default:
                    return MINIC_CORE_LOWER_ERROR;
                }
                instruction.span = expression->span;
                instruction.type = stored_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.binary.left = current;
                instruction.value.binary.right = right;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &result)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
            instruction.span = expression->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.store.address = address;
            instruction.value.store.stored_value = result;
            instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &instruction)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            *value_id = result;
            return MINIC_CORE_LOWER_OK;
        }
    }

    /* M51_SHIFT_COMPOUND_ASSIGNMENT: shifts use integer promotions on each operand
       independently; unlike arithmetic compound assignments they do not use the
       usual arithmetic conversions to a shared operand type. */
    /* BATCH_S_ARITHMETIC_COMPOUND_ASSIGNMENT: multiplication, division and
       remainder use the same usual-arithmetic-conversion path already owned by
       += and -=.  The Core arithmetic opcodes already exist; extend the generic
       load/operate/convert/store seam rather than special-casing qtree_depth. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE ||
         expression->value.binary.operator_kind == MINIC_BINARY_REMAINDER ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_XOR ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR)) {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicCoreObjectId address_object;
        MinicCoreObjectId current_object;
        MinicCoreValueId address;
        MinicCoreValueId current;
        MinicCoreValueId current_common;
        MinicCoreValueId right;
        MinicCoreValueId right_common;
        MinicCoreValueId result;
        MinicCoreValueId stored_value;
        MinicCoreLowerStatus status;
        MinicType address_type;
        MinicType common_type;
        MinicType right_type;
        MinicType stored_type;
        bool shift_assignment;

        target = minic_c0_program_expression(context->body->program, expression->value.binary.left);
        source =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_equal(expression->type, target->type) ||
            minic_type_is_const(target->type) ||
            !minic_type_unqualified(target->type, &stored_type) ||
            !minic_type_is_integer(stored_type) || !minic_type_is_integer(source->type) ||
            context->target == NULL) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        shift_assignment =
            expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
            expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT;
        if (shift_assignment) {
            if (!minic_target_info_integer_promotion_for_program(
                    context->target, context->body->program, stored_type, &common_type) ||
                !minic_target_info_integer_promotion_for_program(
                    context->target, context->body->program, source->type, &right_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        } else {
            if (!minic_target_info_integer_common_for_program(context->target,
                                                              context->body->program,
                                                              stored_type,
                                                              source->type,
                                                              &common_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            right_type = common_type;
        }
        status = lower_address(context, expression->value.binary.left, &address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
        instruction.span = target->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.load.address = address;
        instruction.value.load.is_volatile = minic_type_is_volatile(target->type);
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &current)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status =
            minic_core_lower_internal_append_integer_conversion(context, target->span, common_type, current, &current_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (address >= context->function->value_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        address_type = context->function->values[address].type;
        status = minic_core_lower_internal_spill_scalar_value(context, target->span, address_type, address, &address_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            minic_core_lower_internal_spill_scalar_value(context, target->span, common_type, current_common, &current_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            minic_core_lower_internal_append_integer_conversion(context, source->span, right_type, right, &right_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_reload_scalar_value(
            context, target->span, common_type, current_object, &current_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_reload_scalar_value(context, target->span, address_type, address_object, &address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_ADD:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
            break;
        case MINIC_BINARY_SUBTRACT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
            break;
        case MINIC_BINARY_MULTIPLY:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY;
            break;
        case MINIC_BINARY_DIVIDE:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE;
            break;
        case MINIC_BINARY_REMAINDER:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER;
            break;
        case MINIC_BINARY_SHIFT_LEFT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
            break;
        case MINIC_BINARY_SHIFT_RIGHT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
            break;
        case MINIC_BINARY_BITWISE_AND:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
            break;
        case MINIC_BINARY_BITWISE_XOR:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR;
            break;
        case MINIC_BINARY_BITWISE_OR:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
            break;
        default:
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.span = expression->span;
        instruction.type = common_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current_common;
        instruction.value.binary.right = right_common;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &result)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = minic_core_lower_internal_append_integer_conversion(
            context, expression->span, stored_type, result, &stored_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = address;
        instruction.value.store.stored_value = stored_value;
        instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = stored_value;
        return MINIC_CORE_LOWER_OK;
    }
    if (minic_type_is_integer(expression->type) && context->target != NULL) {
        MinicConstValue constant;
        uint64_t constant_bits;

        if (minic_const_eval_integer(
                context->body->program, context->target, expression_id, &constant) &&
            minic_type_equal(constant.type, expression->type)) {
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            instruction.span = expression->span;
            instruction.type = expression->type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            constant_bits = constant.bits;
            (void)memcpy(&instruction.value.integer_value,
                         &constant_bits,
                         sizeof(instruction.value.integer_value));
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }
    return MINIC_CORE_LOWER_UNSUPPORTED;
}

static MinicCoreLowerStatus core_trace_expression_statement_status(
    const MinicCoreLowerContext *context,
    const MinicExpression *expression,
    const char *route,
    MinicCoreLowerStatus status) {
    if (status == MINIC_CORE_LOWER_UNSUPPORTED && context != NULL &&
        context->source_function != NULL && context->source_function->name != NULL &&
        expression != NULL && route != NULL) {
        (void)fprintf(stderr,
                      "CORE_EXPR_STMT_DETAIL function=%s route=%s expression_kind=%d "
                      "value_category=%d\n",
                      context->source_function->name,
                      route,
                      (int)expression->kind,
                      (int)expression->value_category);
    }
    return status;
}

/* P1_GENERIC_PURE_DISCARDED_ADDR_EQ_V1:
 * A discarded comparison of addresses of two plain automatic locals is
 * effect-free. The AST has already passed type/sema verification. Reject
 * wrappers, calls, pointer arithmetic, pointer dereferences, subscript,
 * compound literals and anything else which might evaluate an effect.
 * Do not skip a full expression on the basis of a guessed alias relation.
 * This optimization is opt-in until the RV64 semantic/frame gate passes.
 */
static bool core_p1_plain_local_address(const MinicC0Program *program,
                                        MinicExpressionId expression_id) {
    const MinicExpression *addr;
    const MinicExpression *local;
    if (program == NULL || expression_id == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    addr = minic_c0_program_expression(program, expression_id);
    if (addr == NULL || addr->kind != MINIC_EXPRESSION_ADDRESS_OF ||
        addr->value.unary.operand == MINIC_EXPRESSION_INVALID ||
        !minic_type_is_pointer(addr->type)) {
        return false;
    }
    local = minic_c0_program_expression(program, addr->value.unary.operand);
    return local != NULL && local->kind == MINIC_EXPRESSION_LOCAL &&
           local->value_category == MINIC_VALUE_LVALUE;
}

static bool core_p1_pure_discarded_local_address_comparison(
    const MinicCoreLowerContext *context, const MinicExpression *expression) {
    const MinicExpression *comparison;
    const MinicC0Program *program;
    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL ||
        expression == NULL || expression->kind != MINIC_EXPRESSION_DISCARD ||
        expression->value.unary.operand == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    comparison = minic_c0_program_expression(program, expression->value.unary.operand);
    if (comparison == NULL || comparison->kind != MINIC_EXPRESSION_BINARY ||
        (comparison->value.binary.operator_kind != MINIC_BINARY_EQUAL &&
         comparison->value.binary.operator_kind != MINIC_BINARY_NOT_EQUAL)) {
        return false;
    }
    return core_p1_plain_local_address(program, comparison->value.binary.left) &&
           core_p1_plain_local_address(program, comparison->value.binary.right);
}

MinicCoreLowerStatus minic_core_lower_internal_lower_expression_statement(MinicCoreLowerContext *context,
                                                       const MinicStatement *statement) {
    const MinicExpression *expression;
    MinicExpressionId source_id;
    MinicExpressionId target_id;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        statement == NULL || statement->expression == MINIC_EXPRESSION_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, statement->expression);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    /* M123_VARIADIC_ARGUMENT_ADDRESS: GNU va builtins are parsed and
       type-checked by frontend/Sema. Core owns their target-neutral execution
       semantics while the selected ABI/backend owns the register save area.
       The current frontend va_list model is a modifiable pointer lvalue. */
    /* SQLite's GNU atomic store: a distinct Core memory effect, not an
     * ordinary scalar assignment that later passes might erase or reorder.
     * Evaluate address and value exactly once in source order. RV64 rejects
     * this instruction until target-correct atomic lowering exists. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_ATOMIC_STORE) {
        const MinicExpression *address_expression;
        MinicCoreValueId address, value;
        MinicCoreObjectId stored_object;
        MinicCoreInstruction atomic;
        MinicType pointee, target_type;
        MinicCoreLowerStatus status;
        address_expression = minic_c0_program_expression(
            context->body->program, expression->value.atomic_store.address);
        if (address_expression == NULL ||
            !minic_type_pointee(address_expression->type, &pointee) ||
            !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) || minic_type_is_const(pointee) ||
            expression->value.atomic_store.memory_order != 0U ||
            !minic_type_unqualified(pointee, &target_type))
            return MINIC_CORE_LOWER_UNSUPPORTED;
        /* The value may contain short-circuit operators that split the CFG.
         * Like ordinary scalar assignments, spill its final value before
         * evaluating the address, then reload in the final basic block.
         * GNU builtin arguments have no specified mutual evaluation order:
         * choosing value first preserves single evaluation without referring
         * to a Core SSA value from a predecessor block. */
        status = minic_core_lower_internal_lower_scalar_assignment_value(
            context, target_type, expression->value.atomic_store.source, &value);
        if (status != MINIC_CORE_LOWER_OK) return status;
        status = minic_core_lower_internal_spill_scalar_value(
            context, expression->span, target_type, value, &stored_object);
        if (status != MINIC_CORE_LOWER_OK) return status;
        status = lower_expression(context, expression->value.atomic_store.address, &address);
        if (status != MINIC_CORE_LOWER_OK) return status;
        status = minic_core_lower_internal_reload_scalar_value(
            context, expression->span, target_type, stored_object, &value);
        if (status != MINIC_CORE_LOWER_OK) return status;
        (void)memset(&atomic, 0, sizeof(atomic));
        atomic.kind = MINIC_CORE_INSTRUCTION_ATOMIC_STORE;
        atomic.span = expression->span;
        atomic.type = minic_type_void();
        atomic.result = MINIC_CORE_VALUE_INVALID;
        atomic.value.atomic_store.address = address;
        atomic.value.atomic_store.stored_value = value;
        atomic.value.atomic_store.memory_order = 0U;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &atomic)
                   ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_START) {
        const MinicExpression *target;
        MinicCoreInstruction instruction;
        MinicCoreLowerStatus status;
        MinicCoreValueId cursor_value;
        MinicCoreValueId target_address;
        MinicType value_type;

        target_id = expression->value.unary.operand;
        target = minic_c0_program_expression(context->body->program, target_id);
        if (context->source_function == NULL || !context->source_function->is_variadic ||
            target == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_pointer(target->type) || minic_type_is_const(target->type) ||
            !minic_type_unqualified(target->type, &value_type) ||
            !minic_type_is_pointer(value_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_address(context, target_id, &target_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &cursor_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = target_address;
        instruction.value.store.stored_value = cursor_value;
        instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_COPY) {
        /* Pointer assignment aliases x86 SysV's mutable 24-byte cursor
         * instead of copying it. Reject until an independent tag-copy
         * operation has been implemented; keep RV64 semantics unchanged. */
        if (context->target != NULL &&
            context->target->architecture == MINIC_TARGET_ARCH_X86_64)
            return MINIC_CORE_LOWER_UNSUPPORTED;
        return minic_core_lower_assignment_pair(context,
                                     expression->value.binary.left,
                                     expression->value.binary.right,
                                     expression->span,
                                     NULL);
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_END) {
        MinicCoreValueId discarded_address;

        return lower_address(context, expression->value.unary.operand, &discarded_address);
    }

    /* M122_DISCARDED_COMMA_EFFECT_SEQUENCE: the C comma operator is an
       explicit sequencing boundary. In expression-statement context its final
       value is discarded, so Core must preserve only the ordered effects:
       evaluate the left operand, then the right operand. Delegate each side
       back through the existing discarded-expression owner rather than forcing
       a scalar SSA result for void/aggregate right operands. Nested comma
       chains naturally recurse through the same path. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_COMMA) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicStatement discarded;
        MinicCoreLowerStatus status;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }

        discarded = *statement;
        discarded.kind = MINIC_STATEMENT_EXPRESSION;
        discarded.target_expression = MINIC_EXPRESSION_INVALID;
        discarded.target_statement = MINIC_STATEMENT_INVALID;
        discarded.inline_asm_id = MINIC_INLINE_ASM_INVALID;
        discarded.then_block = MINIC_BLOCK_INVALID;
        discarded.else_block = MINIC_BLOCK_INVALID;
        discarded.expression = expression->value.binary.left;
        discarded.span = left_expression->span;
        status = minic_core_lower_internal_lower_expression_statement(context, &discarded);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (context->block_id >= context->function->block_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (context->function->blocks[context->block_id].has_terminator) {
            return MINIC_CORE_LOWER_OK;
        }

        discarded.expression = expression->value.binary.right;
        discarded.span = right_expression->span;
        return minic_core_lower_internal_lower_expression_statement(context, &discarded);
    }
    /* M129_LEAF_EXPRESSION_OWNERS: keep M86B as the default discarded-record
       assignment owner. Only conditional aggregate RHS values need the newer
       unified materialization owner here; direct record-return calls need the
       same owner when their result is discarded after all call side effects. */
    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT &&
        minic_type_is_record(expression->type)) {
        const MinicExpression *record_source;

        record_source = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (record_source != NULL &&
            record_source->kind == MINIC_EXPRESSION_CONDITIONAL) {
            MinicCoreValueId discarded_record_address;
            MinicCoreLowerStatus status;

            status = minic_core_lower_internal_record_materialized_address(
                context, statement->expression, &discarded_record_address);
            return core_trace_expression_statement_status(
                context,
                expression,
                "discarded-record-conditional-assignment",
                status);
        }
    }
    if (expression->kind == MINIC_EXPRESSION_CALL &&
        minic_type_is_record(expression->type) &&
        expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
        MinicCoreValueId discarded_record_address;
        MinicCoreLowerStatus status;

        status = minic_core_lower_internal_record_materialized_address(
            context, statement->expression, &discarded_record_address);
        return core_trace_expression_statement_status(
            context, expression, "discarded-record-call", status);
    }

    /* M86B_RECORD_ASSIGNMENT_EXPRESSION_STATEMENT: a record assignment used as
       an expression statement has the same storage effect as RECORD_COPY; its
       aggregate expression result is discarded, so Core does not need an
       aggregate SSA value. This also lets M86 direct-record-call result objects
       feed ordinary `lhs = call_returning_record()` statements. */
    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT &&
        minic_type_is_record(expression->type)) {
        const MinicExpression *record_target;
        const MinicExpression *record_source;
        MinicStatement record_copy;

        record_target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        record_source = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (record_target == NULL || record_source == NULL ||
            !minic_type_is_record(record_target->type) ||
            !minic_type_is_record(record_source->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        record_copy = *statement;
        record_copy.kind = MINIC_STATEMENT_RECORD_COPY;
        record_copy.span = expression->span;
        record_copy.target_expression = expression->value.binary.left;
        record_copy.expression = expression->value.binary.right;
        return lower_record_copy_statement(context, &record_copy);
    }

    /* M91_BUILTIN_UNREACHABLE_TERMINATOR: GNU C marks this control-flow
       point unreachable. Preserve that fact in Core rather than rejecting the
       void expression or inventing a target-specific trap. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNREACHABLE) {
        MinicCoreTerminator terminator;

        if (!minic_type_is_void(expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&terminator, 0, sizeof(terminator));
        terminator.kind = MINIC_CORE_TERMINATOR_UNREACHABLE;
        terminator.span = expression->span;
        terminator.return_value = MINIC_CORE_VALUE_INVALID;
        terminator.return_object = MINIC_CORE_OBJECT_INVALID;
        terminator.branch_target = MINIC_CORE_BLOCK_INVALID;
        terminator.conditional.condition = MINIC_CORE_VALUE_INVALID;
        terminator.conditional.when_true = MINIC_CORE_BLOCK_INVALID;
        terminator.conditional.when_false = MINIC_CORE_BLOCK_INVALID;
        return minic_core_function_set_terminator(
                   context->function, context->block_id, &terminator)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

    /* BATCH_Q_DISCARDED_RECORD_CALL: a direct call returning an aggregate still
       executes when its value is discarded by an expression statement. Core
       already models the returned aggregate as an address-backed result object;
       statement context simply does not consume that object. Keep indirect
       record returns fail-closed until their ABI/object result seam exists. */
    if (expression->kind == MINIC_EXPRESSION_CALL) {
        if (minic_type_is_record(expression->type) &&
            expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
            MinicCoreObjectId discarded_object;

            return minic_core_lower_direct_record_call_object(context, expression, &discarded_object);
        }
        {
            const MinicFunction *callee;
            MinicCoreValueId discarded_value;
            MinicCoreLowerStatus status;

            status = lower_expression(context, statement->expression, &discarded_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return core_trace_expression_statement_status(
                    context, expression, "call", status);
            }
            if (expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
                return MINIC_CORE_LOWER_OK;
            }
            callee = minic_c0_program_function(
                context->body->program, expression->value.call.function_id);
            if (callee == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (callee->is_noreturn) {
                MinicCoreTerminator terminator;

                (void)memset(&terminator, 0, sizeof(terminator));
                terminator.kind = MINIC_CORE_TERMINATOR_UNREACHABLE;
                terminator.span = expression->span;
                terminator.return_value = MINIC_CORE_VALUE_INVALID;
                terminator.return_object = MINIC_CORE_OBJECT_INVALID;
                terminator.branch_target = MINIC_CORE_BLOCK_INVALID;
                terminator.conditional.condition = MINIC_CORE_VALUE_INVALID;
                terminator.conditional.when_true = MINIC_CORE_BLOCK_INVALID;
                terminator.conditional.when_false = MINIC_CORE_BLOCK_INVALID;
                if (!minic_core_function_set_terminator(
                        context->function, context->block_id, &terminator)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            return MINIC_CORE_LOWER_OK;
        }
    }
    /* M54_VOID_CONDITIONAL_STATEMENT: expression statements are only an
       effect boundary. Once M53 can lower a void conditional expression, the
       statement layer must delegate rather than reject the expression kind. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        minic_type_is_void(expression->type)) {
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        status = lower_expression(context, statement->expression, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        return discarded_value == MINIC_CORE_VALUE_INVALID ? MINIC_CORE_LOWER_OK
                                                            : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT &&
        minic_type_is_record(expression->type)) {
        const MinicBlock *statement_block;
        const MinicExpression *statement_result;
        MinicCoreValueId discarded_address;
        MinicCoreLowerStatus status;
        bool statement_expression_terminated;

        statement_block = minic_c0_program_block(
            context->body->program, expression->value.statement_expression.block);
        statement_result = minic_c0_program_expression(
            context->body->program, expression->value.statement_expression.result);
        if (statement_block == NULL || statement_result == NULL ||
            !minic_type_is_record(statement_result->type) ||
            statement_result->type.record_id != expression->type.record_id) {
            return MINIC_CORE_LOWER_ERROR;
        }
        statement_expression_terminated = false;
        status = minic_core_lower_internal_lower_block(context, statement_block, &statement_expression_terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (statement_expression_terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return minic_core_lower_internal_record_materialized_address(
            context, expression->value.statement_expression.result, &discarded_address);
    }
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        status = lower_expression(context, statement->expression, &discarded_value);
        return core_trace_expression_statement_status(
            context, expression, "compound-assignment", status);
    }
    if (expression->kind == MINIC_EXPRESSION_DISCARD) {
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        /* Opt-in, general lowering rule; original Core behaviour is unchanged
           unless the caller explicitly requests this semantic experiment. */
        if (getenv("MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ") != NULL &&
            core_p1_pure_discarded_local_address_comparison(context, expression)) {
            return MINIC_CORE_LOWER_OK;
        }
        status = lower_expression(context, statement->expression, &discarded_value);
        return core_trace_expression_statement_status(context, expression, "discard", status);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT)) {
        MinicCoreValueId discarded_value;

        return core_trace_expression_statement_status(
            context,
            expression,
            "scalar-update",
            minic_core_lower_scalar_update(context, expression, &discarded_value));
    }
    if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {
        MinicCoreValueId discarded_value;
        MinicType discarded_type;

        if (expression->kind == MINIC_EXPRESSION_STATEMENT &&
            minic_type_is_void(expression->type)) {
            const MinicBlock *statement_block;
            const MinicExpression *statement_result;
            MinicCoreLowerStatus block_status;
            bool statement_expression_terminated;

            /* BATCH_Y_VOID_STATEMENT_EXPRESSION_RESULT: the parser removes a
               GNU statement-expression's final expression from its block and
               stores it as `result`.  Effect-only lowering must therefore run
               both pieces in source order.  A final void call is a real side
               effect even though the enclosing expression has no scalar value. */
            statement_block = minic_c0_program_block(
                context->body->program, expression->value.statement_expression.block);
            if (statement_block == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            statement_expression_terminated = false;
            block_status = minic_core_lower_internal_lower_block(context, statement_block, &statement_expression_terminated);
            if (block_status != MINIC_CORE_LOWER_OK || statement_expression_terminated ||
                expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
                return block_status;
            }
            statement_result = minic_c0_program_expression(
                context->body->program, expression->value.statement_expression.result);
            if (statement_result == NULL || !minic_type_is_void(statement_result->type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            return lower_expression(context,
                                    expression->value.statement_expression.result,
                                    &discarded_value);
        }
        if (!core_scalar_expression_value_type(context->body, expression, &discarded_type)) {
            return core_trace_expression_statement_status(
                context, expression, "scalar-type-gate", MINIC_CORE_LOWER_UNSUPPORTED);
        }
        (void)discarded_type;
        return core_trace_expression_statement_status(
            context,
            expression,
            "generic-scalar",
            lower_expression(context, statement->expression, &discarded_value));
    }
    target_id = expression->value.binary.left;
    source_id = expression->value.binary.right;
    return core_trace_expression_statement_status(
        context,
        expression,
        "assignment",
        minic_core_lower_assignment_pair(context, target_id, source_id, expression->span, NULL));
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
                status = lower_local_object(
                    context, expression->value.local_id, &terminator.return_object);
            } else if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL &&
                       minic_c0_record_value_is_address_backed(
                           context->body->program, statement->expression)) {
                status = lower_record_compound_literal_object(
                    context, expression, &terminator.return_object);
            } else if (expression->kind == MINIC_EXPRESSION_CALL &&
                       expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
                status = minic_core_lower_direct_record_call_object(
                    context, expression, &terminator.return_object);
            } else if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
                       minic_type_is_record(expression->type)) {
                status = lower_record_conditional_object(
                    context, expression, &terminator.return_object);
            } else if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT ||
                       minic_c0_record_value_is_address_backed(
                           context->body->program, statement->expression)) {
                status = lower_record_load_object(
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
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND) {
        const MinicExpression *left_expression;
        MinicConstValue left_constant;
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
            if (left_is_zero) {
                return minic_core_lower_internal_set_branch(context, context->block_id, span, when_false);
            }
            return minic_core_lower_internal_lower_condition_branch(
                context, expression->value.binary.right, span, when_true, when_false);
        }
        if (!minic_core_function_add_block(context->function, &right_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = minic_core_lower_internal_lower_condition_branch(
            context, expression->value.binary.left, span, right_block, when_false);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        context->block_id = right_block;
        return minic_core_lower_internal_lower_condition_branch(
            context, expression->value.binary.right, span, when_true, when_false);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {
        const MinicExpression *left_expression;
        MinicConstValue left_constant;
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
            if (!left_is_zero) {
                return minic_core_lower_internal_set_branch(context, context->block_id, span, when_true);
            }
            return minic_core_lower_internal_lower_condition_branch(
                context, expression->value.binary.right, span, when_true, when_false);
        }
        if (!minic_core_function_add_block(context->function, &right_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = minic_core_lower_internal_lower_condition_branch(
            context, expression->value.binary.left, span, when_true, right_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        context->block_id = right_block;
        return minic_core_lower_internal_lower_condition_branch(
            context, expression->value.binary.right, span, when_true, when_false);
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

/* M187_CONSTANT_IF_EXTERNAL_REENTRY: mark every statement owned by a
   discarded block graph so internal gotos disappear with that graph rather
   than falsely keeping an otherwise constant-dead branch alive. */
static bool core_mark_discarded_if_membership(
    const MinicCoreLowerContext *context,
    const MinicBlock *block,
    bool *visited_blocks,
    bool *statement_membership) {
    const MinicC0Program *program;
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        block == NULL || visited_blocks == NULL || statement_membership == NULL) {
        return false;
    }
    program = context->body->program;
    for (index = 0U; index < block->statement_count; ++index) {
        MinicStatementId statement_id = block->statements[index];
        const MinicStatement *statement;

        if (statement_id >= program->statement_count) {
            return false;
        }
        statement_membership[statement_id] = true;
        statement = minic_c0_program_statement(program, statement_id);
        if (statement == NULL) {
            return false;
        }
        if (statement->then_block != MINIC_BLOCK_INVALID) {
            const MinicBlock *child;
            if (statement->then_block >= program->block_count) {
                return false;
            }
            if (!visited_blocks[statement->then_block]) {
                visited_blocks[statement->then_block] = true;
                child = minic_c0_program_block(program, statement->then_block);
                if (child == NULL ||
                    !core_mark_discarded_if_membership(
                        context, child, visited_blocks, statement_membership)) {
                    return false;
                }
            }
        }
        if (statement->else_block != MINIC_BLOCK_INVALID) {
            const MinicBlock *child;
            if (statement->else_block >= program->block_count) {
                return false;
            }
            if (!visited_blocks[statement->else_block]) {
                visited_blocks[statement->else_block] = true;
                child = minic_c0_program_block(program, statement->else_block);
                if (child == NULL ||
                    !core_mark_discarded_if_membership(
                        context, child, visited_blocks, statement_membership)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool core_block_contains_reentry_label(const MinicCoreLowerContext *context,
                                              const MinicBlock *block) {
    const MinicC0Program *program;
    bool *visited_blocks;
    bool *statement_membership;
    bool unsafe;
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        block == NULL) {
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
    unsafe = !core_mark_discarded_if_membership(
        context, block, visited_blocks, statement_membership);

    if (minic_core_reentry_index_ready(context->reentry_index, program)) {
        minic_core_reentry_index_check_discarded(
            context->reentry_index, program, statement_membership, &unsafe);
    } else {
        /* case/default labels are entered by switch dispatch, independently of
           lexical fallthrough, so never prune a subtree that owns one. */
        for (index = 0U; index < program->statement_count && !unsafe; ++index) {
            const MinicStatement *statement;
            if (!statement_membership[index]) {
                continue;
            }
            statement = minic_c0_program_statement(program, index);
            if (statement == NULL || statement->kind == MINIC_STATEMENT_CASE ||
                statement->kind == MINIC_STATEMENT_DEFAULT) {
                unsafe = true;
            }
        }

        /* Only jumps originating outside the discarded membership are external
           re-entry. Internal goto/asm-goto edges disappear with the subtree. */
        for (index = 0U; index < program->statement_count && !unsafe; ++index) {
            const MinicStatement *source;
            if (statement_membership[index]) {
                continue;
            }
            source = minic_c0_program_statement(program, index);
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
                const MinicInlineAsm *inline_asm = &program->inline_asms[source->inline_asm_id];
                size_t label_index;
                if (!inline_asm->is_goto) {
                    continue;
                }
                for (label_index = 0U; label_index < inline_asm->label_count; ++label_index) {
                    MinicStatementId target = inline_asm->labels[label_index].target_statement;
                    if (target < program->statement_count && statement_membership[target]) {
                        unsafe = true;
                        break;
                    }
                }
            }
        }

        /* An address-taken label may be reached through computed goto from code
           that is not represented as a direct target edge here. Keep it alive. */
        for (index = 0U; index < program->expression_count && !unsafe; ++index) {
            const MinicExpression *expression = minic_c0_program_expression(program, index);
            if (expression == NULL) {
                unsafe = true;
                break;
            }
            if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS &&
                expression->value.label_statement_id < program->statement_count &&
                statement_membership[expression->value.label_statement_id]) {
                unsafe = true;
            }
        }

    }

    free(visited_blocks);
    free(statement_membership);
    return unsafe;
}


/* M192_IF_LOCAL_FACT_MEET */
static bool core_expression_may_modify_local(const MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicLocalId local_id,
                                             unsigned int depth);

static void core_local_constants_prepare_if_branch(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *incoming,
    MinicExpressionId condition_expression) {
    size_t index;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || incoming == NULL) {
        return;
    }
    for (index = 0U; index < context->source_function->local_count; ++index) {
        MinicLocalId local_id = context->source_function->local_begin + index;
        context->local_integer_constants[index] = incoming[index];
        if (incoming[index].escaped ||
            core_expression_may_modify_local(
                context, condition_expression, local_id, 0U)) {
            context->local_integer_constants[index].known = false;
            /* Structural branch lowering happens before condition lowering.
               Treat a condition that may expose/modify this local as escaped
               for branch fact purposes; this is conservative and prevents a
               later branch assignment from manufacturing an unsafe fact. */
            context->local_integer_constants[index].escaped = true;
        }
    }
}

static void core_local_constants_meet_if_paths(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *then_facts,
    bool then_continues,
    const MinicCoreLocalIntegerConstant *else_facts,
    bool else_continues) {
    size_t index;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL) {
        return;
    }
    if (then_continues && !else_continues && then_facts != NULL) {
        minic_core_local_facts_restore(context, then_facts);
        return;
    }
    if (!then_continues && else_continues && else_facts != NULL) {
        minic_core_local_facts_restore(context, else_facts);
        return;
    }
    if (!then_continues && !else_continues) {
        minic_core_local_facts_clear_known(context);
        return;
    }
    if (then_facts == NULL || else_facts == NULL) {
        minic_core_local_facts_clear_known(context);
        return;
    }

    for (index = 0U; index < context->source_function->local_count; ++index) {
        const MinicCoreLocalIntegerConstant *left = &then_facts[index];
        const MinicCoreLocalIntegerConstant *right = &else_facts[index];
        MinicCoreLocalIntegerConstant merged = *left;
        bool same_value =
            left->known && right->known &&
            !left->escaped && !right->escaped &&
            minic_type_equal(left->value.type, right->value.type) &&
            left->value.bits == right->value.bits;

        merged.escaped = left->escaped || right->escaped;
        merged.specialization_modified =
            left->specialization_modified || right->specialization_modified;
        merged.specialization_known =
            left->specialization_known && right->specialization_known &&
            minic_type_equal(left->specialization_value.type,
                             right->specialization_value.type) &&
            left->specialization_value.bits == right->specialization_value.bits;
        merged.known = same_value && !merged.escaped;
        context->local_integer_constants[index] = merged;
    }
}

/* INLINE_ASM_IF_FACT_BARRIER: a side-effecting GNU asm block must
   not be made unconditional by a guessed straight-line local fact. */
static bool core_block_contains_inline_asm_impl(
    const MinicCoreLowerContext *context,
    const MinicBlock *block,
    bool *visited) {
    size_t index;
    const MinicC0Program *program;
    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL ||
        block == NULL || visited == NULL) {
        return true;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        const MinicStatement *statement = minic_c0_program_statement(
            program, block->statements[index]);
        const MinicBlock *nested;
        if (statement == NULL) {
            return true;
        }
        /* The parser normalizes do { asm } while (0) to a synthetic
           WHILE. Its inline effects can be hidden behind an internal
           one-pass form, so treat any dynamic structured loop as an
           optimization barrier, not as an unsafe constant-fold witness. */
        if (statement->kind == MINIC_STATEMENT_INLINE_ASM ||
            statement->kind == MINIC_STATEMENT_WHILE) {
            return true;
        }
        if (statement->then_block != MINIC_BLOCK_INVALID) {
            if (statement->then_block >= program->block_count) {
                return true;
            }
            if (!visited[statement->then_block]) {
                visited[statement->then_block] = true;
                nested = minic_c0_program_block(program, statement->then_block);
                if (core_block_contains_inline_asm_impl(context, nested, visited)) {
                    return true;
                }
            }
        }
        if (statement->else_block != MINIC_BLOCK_INVALID) {
            if (statement->else_block >= program->block_count) {
                return true;
            }
            if (!visited[statement->else_block]) {
                visited[statement->else_block] = true;
                nested = minic_c0_program_block(program, statement->else_block);
                if (core_block_contains_inline_asm_impl(context, nested, visited)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool core_block_contains_inline_asm(
    const MinicCoreLowerContext *context, const MinicBlock *block) {
    bool *visited;
    bool contains;
    const MinicC0Program *program;
    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL ||
        block == NULL || program->block_count == 0U) {
        return true;
    }
    visited = (bool *)calloc(program->block_count, sizeof(*visited));
    if (visited == NULL) {
        return true;
    }
    contains = core_block_contains_inline_asm_impl(context, block, visited);
    free(visited);
    return contains;
}


/* RUNTIME_IF_FALLTHROUGH_FACTS_V1: preserve unescaped locals through a
   precisely constrained read-only-local guard or direct call whose arguments
   cannot provide an address of a local being propagated. */
/* A direct function call can mutate global memory, but cannot write a
   currently unescaped automatic local unless its address is supplied.
   Accept only direct function calls whose arguments contain no address-of,
   assignments, increments, volatile reads, or arbitrary expressions.
   The Core fact table independently tracks local address escapes. */
static bool core_runtime_if_guard_preserves_locals_depth(
    const MinicCoreLowerContext *context, MinicExpressionId id,
    unsigned int depth) {
    const MinicC0Program *program;
    const MinicExpression *expression;
    const MinicLocal *local;
    size_t i;

    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || depth > 24U) {
        return false;
    }
    program = context->body->program;
    expression = minic_c0_program_expression(program, id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        return core_runtime_if_guard_preserves_locals_depth(
            context, expression->value.unary.operand, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return core_runtime_if_guard_preserves_locals_depth(
            context, expression->value.unary.operand, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        local = minic_c0_program_local(program, expression->value.local_id);
        return local != NULL && !minic_type_is_volatile(local->type);
    }
    if (expression->kind == MINIC_EXPRESSION_INTEGER) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_CALL) {
        /* Direct calls store the function_id, not a source callee expr.
           Indirect calls have function_id == MINIC_FUNCTION_INVALID. */
        if (expression->value.call.function_id == MINIC_FUNCTION_INVALID ||
            minic_c0_program_function(
                program, expression->value.call.function_id) == NULL) {
            return false;
        }
        for (i = 0U; i < expression->value.call.argument_count; ++i) {
            if (!core_runtime_if_guard_preserves_locals_depth(
                    context, minic_c0_call_argument(expression, i), depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static bool core_runtime_if_pure_local_guard(
    const MinicCoreLowerContext *context, MinicExpressionId id) {
    return core_runtime_if_guard_preserves_locals_depth(context, id, 0U);
}

static MinicCoreLocalIntegerConstant *core_runtime_if_copy_facts(
    const MinicCoreLowerContext *context) {
    MinicCoreLocalIntegerConstant *copy;
    size_t count;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL) {
        return NULL;
    }
    count = context->source_function->local_count;
    if (count == 0U || count > SIZE_MAX / sizeof(*copy)) {
        return NULL;
    }
    copy = (MinicCoreLocalIntegerConstant *)malloc(count * sizeof(*copy));
    if (copy != NULL) {
        (void)memcpy(copy, context->local_integer_constants,
                     count * sizeof(*copy));
    }
    return copy;
}

static void core_runtime_if_merge_false_fallthrough(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *incoming,
    const MinicCoreLocalIntegerConstant *then_exit,
    bool then_terminated) {
    size_t i;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || incoming == NULL) {
        return;
    }
    for (i = 0U; i < context->source_function->local_count; ++i) {
        const MinicCoreLocalIntegerConstant *before = &incoming[i];
        const MinicCoreLocalIntegerConstant *after =
            then_exit == NULL ? NULL : &then_exit[i];
        /* The false edge always continues with incoming local values.
           If the true edge also continues, it must prove the SAME value. */
        if (!before->known || before->escaped ||
            (!then_terminated &&
             (after == NULL || !after->known || after->escaped ||
              !minic_type_equal(before->value.type, after->value.type) ||
              before->value.bits != after->value.bits))) {
            continue;
        }
        context->local_integer_constants[i] = *before;
        if (!then_terminated && after != NULL) {
            context->local_integer_constants[i].specialization_modified =
                before->specialization_modified ||
                after->specialization_modified;
            context->local_integer_constants[i].escaped =
                before->escaped || after->escaped;
            if (context->local_integer_constants[i].specialization_modified) {
                context->local_integer_constants[i].specialization_known = false;
            }
        }
    }
}
static MinicCoreLowerStatus
lower_if(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
    const MinicBlock *else_source;
    const MinicBlock *then_source;
    const MinicExpression *condition_expression;
    MinicCoreBlockId condition_block;
    MinicCoreBlockId else_block;
    MinicCoreBlockId false_target;
    MinicCoreBlockId merge_block;
    MinicCoreBlockId then_block;
    MinicCoreBlockId then_continuation_block;
    MinicCoreBlockId else_continuation_block;
    MinicCoreBlockId continuation_block;
    MinicCoreLowerStatus status;
    MinicCoreLocalIntegerConstant *fallthrough_facts = NULL;
    MinicCoreLocalIntegerConstant *then_exit_facts = NULL;
    MinicCoreLocalIntegerConstant *incoming_facts;
    MinicCoreLocalIntegerConstant *branch_entry_facts;
    MinicCoreLocalIntegerConstant *then_facts;
    MinicCoreLocalIntegerConstant *else_facts;
    bool else_terminated;
    bool needs_merge;
    bool then_terminated;
    bool preserve_guard_facts;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL || terminated == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    condition_expression =
        minic_c0_program_expression(context->body->program, statement->expression);
    then_source = minic_c0_program_block(context->body->program, statement->then_block);
    if (condition_expression == NULL || then_source == NULL ||
        (!minic_type_is_integer(condition_expression->type) &&
         !minic_type_is_pointer(condition_expression->type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    else_source = NULL;
    if (statement->else_block != MINIC_BLOCK_INVALID) {
        else_source = minic_c0_program_block(context->body->program, statement->else_block);
        if (else_source == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    if (statement->cleanup_context == statement->cleanup_stop_context &&
        minic_type_is_integer(condition_expression->type)) {
        bool condition_is_zero;
        /* Independent zero-only bool reachability: keep the original
           Runtime-profile integer and pointer if-seams byte-identical. */
        if (core_zero_bool_cfg_condition(
                context, statement->expression, 0U, &condition_is_zero)) {
            const MinicBlock *selected_source =
                condition_is_zero ? else_source : then_source;
            const MinicBlock *discarded_source =
                condition_is_zero ? then_source : else_source;
            if (discarded_source == NULL ||
                !core_block_contains_reentry_label(context, discarded_source)) {
                if (selected_source == NULL) {
                    *terminated = false;
                    return MINIC_CORE_LOWER_OK;
                }
                return minic_core_lower_internal_lower_block(context, selected_source, terminated);
            }
        }

    }

    /* M182_TERMINATING_GUARD_FACTS: a pure terminating taken arm does not
       contribute state to the following statement. */
    preserve_guard_facts = false;
    if (else_source == NULL && then_source->statement_count == 1U &&
        minic_core_cfg_pure_call_argument(context, statement->expression, 0U)) {
        const MinicStatement *guard_statement = minic_c0_program_statement(
            context->body->program, then_source->statements[0]);
        preserve_guard_facts =
            guard_statement != NULL &&
            (guard_statement->kind == MINIC_STATEMENT_GOTO ||
             guard_statement->kind == MINIC_STATEMENT_BREAK);
    }

    /* MINIC_DMA_IF_TRACE_V0 */
    if (getenv("MINIC_DMA_IF_TRACE") != NULL &&
        context->source_function != NULL &&
        context->source_function->name != NULL &&
        strcmp(context->source_function->name, "dma_direct_alloc") == 0) {
        MinicConstValue traced;
        MinicLocalId traced_local = MINIC_LOCAL_INVALID;
        size_t fact_idx = 0U;
        bool local_read = minic_core_lower_direct_local_read(
            context, statement->expression, &traced_local);
        bool fact_local = local_read &&
            minic_core_local_fact_index(context, traced_local, &fact_idx);
        bool known = core_const_eval_integer_with_locals(
            context, statement->expression, &traced);
        (void)fprintf(stderr,
            "DMA_IF_TRACE line=%zu expr_kind=%d read_local=%d local=%zu known=%d bits=%llu fact_known=%d escaped=%d stable=%d\\n",
            statement->span.begin.line,
            (int)condition_expression->kind, local_read ? 1 : 0,
            (size_t)traced_local, known ? 1 : 0,
            known ? (unsigned long long)traced.bits : 0ULL,
            fact_local && context->local_integer_constants[fact_idx].known ? 1 : 0,
            fact_local && context->local_integer_constants[fact_idx].escaped ? 1 : 0,
            fact_local && context->local_integer_constants[fact_idx].program_zero_stable ? 1 : 0);
    }
    /* MINIC_SHMEM_IF_TRACE_V0 */
    if (getenv("MINIC_SHMEM_IF_TRACE") != NULL &&
        context->source_function != NULL &&
        context->source_function->name != NULL &&
        strcmp(context->source_function->name, "shmem_alloc_and_acct_folio") == 0) {
        MinicConstValue traced;
        MinicLocalId traced_local = MINIC_LOCAL_INVALID;
        size_t fact_idx = 0U;
        bool local_read = minic_core_lower_direct_local_read(
            context, statement->expression, &traced_local);
        bool fact_local = local_read &&
            minic_core_local_fact_index(context, traced_local, &fact_idx);
        bool known = core_const_eval_integer_with_locals(
            context, statement->expression, &traced);
        (void)fprintf(stderr,
            "SHMEM_IF_TRACE line=%zu expr_kind=%d read_local=%d local=%zu known=%d bits=%llu fact_known=%d escaped=%d stable=%d\n",
            statement->span.begin.line,
            (int)condition_expression->kind, local_read ? 1 : 0,
            (size_t)traced_local, known ? 1 : 0,
            known ? (unsigned long long)traced.bits : 0ULL,
            fact_local && context->local_integer_constants[fact_idx].known ? 1 : 0,
            fact_local && context->local_integer_constants[fact_idx].escaped ? 1 : 0,
            fact_local && context->local_integer_constants[fact_idx].program_zero_stable ? 1 : 0);
    }
    /* BusyBox and ordinary GNU C intentionally leave impossible references in
       branches guarded by target constants such as sizeof comparisons and
       ENABLE_* macros.  GCC removes those branches before emission.  Preserve
       the same language-level reachability fact in Core: when the existing
       target-aware integer const-evaluator proves an if condition, lower only
       the selected source block.  A discarded subtree that owns any C/case/
       default label stays on the general CFG path because an enclosing goto or
       switch may legally enter it. */
    if (statement->cleanup_context == statement->cleanup_stop_context) {
        bool condition_is_zero = false;
        bool condition_known = false;

        if (minic_type_is_pointer(condition_expression->type) &&
            minic_core_lower_known_null_pointer(context, statement->expression)) {
            condition_known = true;
            condition_is_zero = true;
        } else if (minic_type_is_integer(condition_expression->type)) {
            MinicConstValue condition_value;
            bool integer_condition_is_zero;

            if ((minic_const_eval_integer(context->body->program,
                                          context->target,
                                          statement->expression, &condition_value) ||
                 (!core_block_contains_inline_asm(context, then_source) &&
                  (else_source == NULL ||
                   !core_block_contains_inline_asm(context, else_source)) &&
                  core_const_eval_integer_with_locals(
                      context, statement->expression, &condition_value))) &&
                minic_const_value_is_zero(context->body->program,
                                          context->target,
                                          &condition_value,
                                          &integer_condition_is_zero)) {
                condition_known = true;
                condition_is_zero = integer_condition_is_zero;
            }
        }
        if (context->source_function != NULL &&
            context->source_function->name != NULL &&
            strcmp(context->source_function->name, "genl_op_from_split") == 0) {
            (void)fprintf(stderr,
                          "GENL_IF_DECISION expression=%zu kind=%d known=%d zero=%d block=%u\n",
                          (size_t)statement->expression,
                          (int)condition_expression->kind,
                          condition_known ? 1 : 0,
                          condition_is_zero ? 1 : 0,
                          (unsigned int)context->block_id);
        }
        if (condition_known) {
            const MinicBlock *discarded_source;
            const MinicBlock *selected_source;

            selected_source = condition_is_zero ? else_source : then_source;
            discarded_source = condition_is_zero ? then_source : else_source;
            if (discarded_source == NULL ||
                !core_block_contains_reentry_label(context, discarded_source)) {
                if (selected_source == NULL) {
                    *terminated = false;
                    return MINIC_CORE_LOWER_OK;
                }
                return minic_core_lower_internal_lower_block(context, selected_source, terminated);
            }
        }
    }

    fallthrough_facts = NULL;
    then_exit_facts = NULL;
    if (else_source == NULL &&
        core_runtime_if_pure_local_guard(context, statement->expression) &&
        !core_block_contains_reentry_label(context, then_source)) {
        fallthrough_facts = core_runtime_if_copy_facts(context);
    }
    if (getenv("MINIC_SHMEM_IF_TRACE") != NULL &&
        context->source_function != NULL &&
        strcmp(context->source_function->name, "shmem_alloc_and_acct_folio") == 0) {
        size_t dbg_idx;
        if (minic_core_local_fact_index(context, (MinicLocalId)10022U, &dbg_idx)) {
            (void)fprintf(stderr, "SHMEM_IF_FLOW line=%zu edge=entry huge_known=%d val=%llu snap=%d pure=%d reentry=%d\n",
               statement->span.begin.line,
               context->local_integer_constants[dbg_idx].known ? 1 : 0,
               (unsigned long long)context->local_integer_constants[dbg_idx].value.bits,
               fallthrough_facts != NULL ? 1 : 0,
               core_runtime_if_pure_local_guard(context, statement->expression) ? 1 : 0,
               core_block_contains_reentry_label(context, then_source) ? 1 : 0);
        }
    }
    incoming_facts = minic_core_local_facts_snapshot(context);
    if (context->source_function != NULL &&
        context->source_function->local_count != 0U && incoming_facts == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    branch_entry_facts = NULL;
    then_facts = NULL;
    else_facts = NULL;
    if (!preserve_guard_facts) {
        minic_core_local_facts_clear_known(context);
        core_local_constants_prepare_if_branch(
            context, incoming_facts, statement->expression);
    }
    branch_entry_facts = minic_core_local_facts_snapshot(context);
    if (context->source_function != NULL &&
        context->source_function->local_count != 0U && branch_entry_facts == NULL) {
        free(incoming_facts);
        return MINIC_CORE_LOWER_ERROR;
    }
    condition_block = context->block_id;
    if (!minic_core_function_add_block(context->function, &then_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    else_block = MINIC_CORE_BLOCK_INVALID;
    if (else_source != NULL && !minic_core_function_add_block(context->function, &else_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    context->block_id = then_block;
    if (context->source_function && context->source_function->name &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0)
        (void)fprintf(stderr, "GENL_IF_PATH stage=pre_then expr=%zu current=%u then=%u condition=%u\n",
            (size_t)statement->expression, (unsigned int)context->block_id,
            (unsigned int)then_block, (unsigned int)condition_block);
    status = minic_core_lower_internal_lower_block(context, then_source, &then_terminated);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr,
                      "CORE_FAST_TRACE stage=if reason=then-body function=%s "
                      "status=%d condition_kind=%d span=%zu:%zu\n",
                      context->source_function->name,
                      (int)status,
                      (int)condition_expression->kind,
                      statement->span.begin.line,
                      statement->span.begin.column);
        return status;
    }
    if (context->source_function && context->source_function->name &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0)
        (void)fprintf(stderr, "GENL_IF_PATH stage=post_then expr=%zu status=%d terminated=%d continuation=%u\n",
            (size_t)statement->expression, (int)status,
            then_terminated ? 1 : 0, (unsigned int)context->block_id);
    then_continuation_block = context->block_id;
    if (fallthrough_facts != NULL) {
        then_exit_facts = core_runtime_if_copy_facts(context);
    }
    then_facts = minic_core_local_facts_snapshot(context);
    if (context->source_function != NULL &&
        context->source_function->local_count != 0U && then_facts == NULL) {
        free(branch_entry_facts);
        free(incoming_facts);
        return MINIC_CORE_LOWER_ERROR;
    }
    else_continuation_block = MINIC_CORE_BLOCK_INVALID;
    else_terminated = false;
    if (else_source != NULL) {
        minic_core_local_facts_restore(context, branch_entry_facts);
        context->block_id = else_block;
        status = minic_core_lower_internal_lower_block(context, else_source, &else_terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr,
                          "CORE_FAST_TRACE stage=if reason=else-body function=%s "
                          "status=%d condition_kind=%d span=%zu:%zu\n",
                          context->source_function->name,
                          (int)status,
                          (int)condition_expression->kind,
                          statement->span.begin.line,
                          statement->span.begin.column);
            return status;
        }
        else_continuation_block = context->block_id;
        else_facts = minic_core_local_facts_snapshot(context);
        if (context->source_function != NULL &&
            context->source_function->local_count != 0U && else_facts == NULL) {
            free(then_facts);
            free(branch_entry_facts);
            free(incoming_facts);
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    needs_merge = !then_terminated || else_source == NULL || !else_terminated;
    merge_block = MINIC_CORE_BLOCK_INVALID;
    if (needs_merge) {
        if (!minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!then_terminated) {
            status = minic_core_lower_internal_set_branch(context, then_continuation_block, statement->span, merge_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        if (else_source != NULL && !else_terminated) {
            status = minic_core_lower_internal_set_branch(context, else_continuation_block, statement->span, merge_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        false_target = else_source == NULL ? merge_block : else_block;
        context->block_id = merge_block;
    } else {
        false_target = else_block;
        context->block_id = condition_block;
    }

    continuation_block = context->block_id;
    context->block_id = condition_block;
    minic_core_local_facts_restore(context, incoming_facts);
    /* DOMINANCE_IF_GUARD_FACT_BARRIER: speculative body facts do not
       dominate the earlier condition_block. Rebuild the runtime predicate
       with only immutable source/specialization facts, not body updates. */
    minic_core_local_facts_clear_known(context);
    if (context->source_function && context->source_function->name &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0)
        (void)fprintf(stderr, "GENL_IF_PATH stage=pre_guard expr=%zu condition=%u then=%u else=%u need_merge=%d\n",
            (size_t)statement->expression, (unsigned int)condition_block,
            (unsigned int)then_block, (unsigned int)false_target,
            needs_merge ? 1 : 0);
    status = minic_core_lower_internal_lower_condition_branch(
        context, statement->expression, statement->span, then_block, false_target);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr,
                      "CORE_FAST_TRACE stage=if reason=condition function=%s "
                      "status=%d condition_kind=%d span=%zu:%zu\n",
                      context->source_function->name,
                      (int)status,
                      (int)condition_expression->kind,
                      statement->span.begin.line,
                      statement->span.begin.column);
        free(incoming_facts);
        return status;
    }
    context->block_id = continuation_block;
    if (!(else_source == NULL && then_terminated)) {
        core_local_constants_meet_if_paths(
            context,
            then_facts,
            !then_terminated,
            else_source == NULL ? branch_entry_facts : else_facts,
            else_source == NULL ? true : !else_terminated);
    }
    free(else_facts);
    free(then_facts);
    free(branch_entry_facts);
    if (else_source == NULL) {
        core_runtime_if_merge_false_fallthrough(
            context, fallthrough_facts, then_exit_facts, then_terminated);
    }
    free(then_exit_facts);
    free(fallthrough_facts);
    if (getenv("MINIC_SHMEM_IF_TRACE") != NULL &&
        context->source_function != NULL &&
        strcmp(context->source_function->name, "shmem_alloc_and_acct_folio") == 0) {
        size_t dbg_idx;
        if (minic_core_local_fact_index(context, (MinicLocalId)10022U, &dbg_idx)) {
            (void)fprintf(stderr, "SHMEM_IF_FLOW line=%zu edge=merge huge_known=%d val=%llu terminated=%d\n",
                statement->span.begin.line,
                context->local_integer_constants[dbg_idx].known ? 1 : 0,
                (unsigned long long)context->local_integer_constants[dbg_idx].value.bits,
                then_terminated ? 1 : 0);
        }
    }
    free(incoming_facts);
    if (context->source_function && context->source_function->name &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0)
        (void)fprintf(stderr, "GENL_IF_PATH stage=post_guard expr=%zu status=%d block=%u\n",
            (size_t)statement->expression, (int)status,
            (unsigned int)context->block_id);
    *terminated = !needs_merge;
    return MINIC_CORE_LOWER_OK;
}

/* M132_UNBOUNDED_FOR_TERMINATION: structured control-flow reachability is
   the owner of whether a synthetic loop exit can fall through. In particular,
   an omitted-condition GNU/C `for (;;)` has no natural edge to its exit block;
   only an explicit break may make that exit reachable. */
bool minic_core_lower_internal_block_has_predecessor(const MinicCoreFunction *function,
                                       MinicCoreBlockId target) {
    size_t block_index;

    if (function == NULL || target == MINIC_CORE_BLOCK_INVALID ||
        target >= function->block_count) {
        return false;
    }
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        const MinicCoreBlock *block = &function->blocks[block_index];
        if (!block->has_terminator) {
            continue;
        }
        if (block->terminator.kind == MINIC_CORE_TERMINATOR_BRANCH &&
            block->terminator.branch_target == target) {
            return true;
        }
        if (block->terminator.kind == MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH &&
            (block->terminator.conditional.when_true == target ||
             block->terminator.conditional.when_false == target)) {
            return true;
        }
    }
    return false;
}

/* M133_CONSTANT_TRUE_LOOP_REACHABILITY: a loop condition that Sema/const-eval
   proves to be a nonzero integer constant has no natural false edge. Keep this
   a target-neutral CFG fact: explicit break edges still make the synthetic exit
   reachable, while an otherwise unreachable exit terminates the enclosing path. */
static bool core_loop_condition_is_constant_true(const MinicCoreLowerContext *context,
                                                 MinicExpressionId expression_id) {
    MinicConstValue value;
    bool is_zero;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || expression_id == MINIC_EXPRESSION_INVALID ||
        !minic_const_eval_integer(
            context->body->program, context->target, expression_id, &value) ||
        !minic_const_value_is_zero(
            context->body->program, context->target, &value, &is_zero)) {
        return false;
    }
    return !is_zero;
}

static MinicCoreLowerStatus
lower_while(MinicCoreLowerContext *context,
            const MinicStatement *statement,
            MinicStatementId continue_label_statement,
            bool *terminated) {
    const MinicBlock *body_source;
    const MinicBlock *iteration_source;
    const MinicExpression *condition_expression;
    const MinicStatement *for_update;
    MinicBlock normalized_for_body;
    MinicBlock scoped_iteration_body;
    MinicBlock normalized_do_while_body;
    MinicStatementId normalized_do_while_continue;
    MinicStatementId normalized_for_continue;
    MinicCoreBlockId body_block;
    MinicCoreBlockId condition_block;
    MinicCoreBlockId exit_block;
    MinicCoreBlockId preheader_block;
    MinicCoreBlockId update_block;
    MinicCoreBlockId saved_break_target;
    MinicCoreLowerStatus status;
    bool body_terminated;
    bool normalized_for;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL || terminated == NULL ||
        statement->kind != MINIC_STATEMENT_WHILE || !minic_core_loop_cleanup_edge_is_empty(statement) ||
        statement->then_block == MINIC_BLOCK_INVALID ||
        statement->else_block != MINIC_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    body_source = minic_c0_program_block(context->body->program, statement->then_block);
    if (body_source == NULL) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    condition_expression = NULL;
    if (statement->expression != MINIC_EXPRESSION_INVALID) {
        condition_expression =
            minic_c0_program_expression(context->body->program, statement->expression);
        if (condition_expression == NULL ||
            (!minic_type_is_integer(condition_expression->type) &&
             !minic_type_is_pointer(condition_expression->type))) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    }
    normalized_do_while_continue = MINIC_STATEMENT_INVALID;
    if (minic_core_loop_normalized_do_while_zero_body(context,
                                      statement,
                                      body_source,
                                      &normalized_do_while_body,
                                      &normalized_do_while_continue)) {
        if (!minic_core_loop_normalized_do_while_block_needs_exit(context,
                                                  &normalized_do_while_body,
                                                  normalized_do_while_continue,
                                                  true)) {
            /* Keep the previously proven flattening semantics when BODY has no
               transfer owned by this loop. This avoids manufacturing CFG state
               for the many effect-only do-while(0) macros already accepted. */
            status = minic_core_lower_internal_lower_block(context, &normalized_do_while_body, &body_terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            *terminated = body_terminated;
            return MINIC_CORE_LOWER_OK;
        }
        {
            MinicCoreBlockId single_iteration_exit;

            if (normalized_do_while_continue >= context->statement_block_count ||
                context->statement_blocks == NULL ||
                context->statement_blocks[normalized_do_while_continue] !=
                    MINIC_CORE_BLOCK_INVALID ||
                !minic_core_function_add_block(context->function, &single_iteration_exit)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            context->statement_blocks[normalized_do_while_continue] = single_iteration_exit;
            saved_break_target = context->break_target;
            context->break_target = single_iteration_exit;
            status = minic_core_lower_internal_lower_block(context, &normalized_do_while_body, &body_terminated);
            context->break_target = saved_break_target;
            if (status != MINIC_CORE_LOWER_OK) {
                (void)fprintf(stderr,
                              "CORE_FAST_TRACE stage=do-while-zero reason=body function=%s "
                              "status=%d span=%zu:%zu\n",
                              context->source_function->name,
                              (int)status,
                              statement->span.begin.line,
                              statement->span.begin.column);
                return status;
            }
            if (!body_terminated) {
                status = minic_core_lower_internal_set_branch(
                    context, context->block_id, statement->span, single_iteration_exit);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            context->block_id = single_iteration_exit;
            *terminated = false;
            return MINIC_CORE_LOWER_OK;
        }
    }

    iteration_source = body_source;
    for_update = NULL;
    normalized_for = false;
    normalized_for_continue = MINIC_STATEMENT_INVALID;
    if (minic_core_loop_normalized_for_update_tail(
            context, statement, body_source, &normalized_for_body, &for_update)) {
        iteration_source = &normalized_for_body;
        normalized_for = true;
    } else if (minic_core_loop_normalized_for_continue_tail(
                   context, statement, body_source, &normalized_for_body)) {
        iteration_source = &normalized_for_body;
        normalized_for = true;
    }
    if (!normalized_for &&
        minic_core_loop_normalized_do_while_true_body(context,
                                      statement,
                                      body_source,
                                      &normalized_do_while_body,
                                      &normalized_do_while_continue)) {
        if (continue_label_statement != MINIC_STATEMENT_INVALID &&
            continue_label_statement != normalized_do_while_continue) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        continue_label_statement = normalized_do_while_continue;
        iteration_source = &normalized_do_while_body;
    }
    /* M149_PARSER_TAIL_CONTINUE_PROVENANCE_OWNER: minic_core_loop_normalized_for_update_tail
       and minic_core_loop_normalized_for_continue_tail have already proven the exact parser
       shape and same-span synthetic LABEL at the source for-loop tail.  Use
       that statement id directly; never recover identity by scanning GOTOs.
       If the adjacent-label path also supplied an id, require exact agreement.
       This keeps ordinary while ownership on its pre-M137 adjacent-label path
       while giving detached normalized-for views one structural provenance. */
    if (normalized_for) {
        size_t continue_tail_distance = for_update != NULL ? 2U : 1U;

        if (body_source->statement_count < continue_tail_distance) {
            return MINIC_CORE_LOWER_ERROR;
        }
        normalized_for_continue =
            body_source->statements[body_source->statement_count - continue_tail_distance];
        if (normalized_for_continue == MINIC_STATEMENT_INVALID ||
            normalized_for_continue >= context->statement_block_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (continue_label_statement != MINIC_STATEMENT_INVALID &&
            continue_label_statement != normalized_for_continue) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        continue_label_statement = normalized_for_continue;
    }
    /* M141_SCOPED_LOOP_CONTINUE_TAIL_OWNER: M137 has already proven that
       continue_label_statement is the unique parser-owned continue target for
       this exact loop and lower_while will bind it to condition_block before
       lowering the body. If the normalized-for view still retains that exact
       synthetic label as its sequential tail, remove only that statement from
       the executable iteration view. This prevents generic LABEL lowering from
       switching context->block_id back to the already-terminated condition
       block and falsely clearing body termination. Ordinary labels and loops
       without an M137-proven target are bit-for-bit unchanged. */
    if (normalized_for && continue_label_statement != MINIC_STATEMENT_INVALID &&
        iteration_source != NULL && iteration_source->statement_count > 0U &&
        iteration_source->statements[iteration_source->statement_count - 1U] ==
            continue_label_statement) {
        scoped_iteration_body = *iteration_source;
        scoped_iteration_body.statement_count -= 1U;
        iteration_source = &scoped_iteration_body;
    }
    if (statement->expression == MINIC_EXPRESSION_INVALID && !normalized_for) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    preheader_block = context->block_id;
    update_block = MINIC_CORE_BLOCK_INVALID;
    if (!minic_core_function_add_block(context->function, &condition_block) ||
        !minic_core_function_add_block(context->function, &body_block) ||
        (normalized_for && for_update != NULL &&
         continue_label_statement != MINIC_STATEMENT_INVALID &&
         !minic_core_function_add_block(context->function, &update_block)) ||
        !minic_core_function_add_block(context->function, &exit_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    /* M147_NORMALIZED_FOR_CONTINUE_BINDING_OWNER: M145/M146 proved that the
       same-span detached-label heuristic must not mutate an ordinary source
       WHILE CFG.  Keep binding at the established post-block-allocation point,
       but require parser-normalized `for` provenance first. */
    if (continue_label_statement != MINIC_STATEMENT_INVALID) {
        if (context->statement_blocks == NULL ||
            continue_label_statement >= context->statement_block_count ||
            context->statement_blocks[continue_label_statement] != MINIC_CORE_BLOCK_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* M148_NORMALIZED_FOR_UPDATE_CONTINUE_CFG_OWNER: C `continue` in a
           source for-loop executes the iteration expression before condition
           re-evaluation.  A no-update/unbounded for therefore targets the
           condition block directly, while an update-bearing for with a proven
           continue gets a dedicated update block.  Loops without a recovered
           continue keep the historical inline-update lowering unchanged. */
        context->statement_blocks[continue_label_statement] =
            for_update != NULL ? update_block : condition_block;
    }
    status = minic_core_lower_internal_set_branch(context, preheader_block, statement->span, condition_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = condition_block;
    if (statement->expression == MINIC_EXPRESSION_INVALID ||
        core_loop_condition_is_constant_true(context, statement->expression)) {
        /* C defines an omitted for-condition as true, and a proven nonzero
           integer constant has the same CFG reachability. Keep an explicit
           Core condition block so break/backedge ownership remains identical. */
        status = minic_core_lower_internal_set_branch(context, condition_block, statement->span, body_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        /* M84_SHARED_LOOP_CONDITION_BRANCH: if/while/normalized-for share one
           scalar-condition owner. This admits pointer truth values and keeps
           !/&&/|| short-circuit CFG construction out of the loop lowering. */
        status = minic_core_lower_internal_lower_condition_branch(context,
                                        statement->expression,
                                        statement->span,
                                        body_block,
                                        exit_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }

    context->block_id = body_block;
    saved_break_target = context->break_target;
    context->break_target = exit_block;
    status = minic_core_lower_internal_lower_block(context, iteration_source, &body_terminated);
    context->break_target = saved_break_target;
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr,
                      "CORE_FAST_TRACE stage=while reason=body function=%s status=%d "
                      "normalized_for=%d has_update=%d span=%zu:%zu\n",
                      context->source_function->name,
                      (int)status,
                      normalized_for ? 1 : 0,
                      for_update != NULL ? 1 : 0,
                      statement->span.begin.line,
                      statement->span.begin.column);
        return status;
    }
    if (for_update != NULL && update_block != MINIC_CORE_BLOCK_INVALID) {
        /* M148_NORMALIZED_FOR_UPDATE_CONTINUE_CFG_OWNER: converge both natural
           body fallthrough and every continue edge at one update owner, then
           evaluate the update exactly once before the condition backedge. */
        if (!body_terminated) {
            status = minic_core_lower_internal_set_branch(context, context->block_id, statement->span, update_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        context->block_id = update_block;
        status = minic_core_lower_internal_lower_expression_statement(context, for_update);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (context->block_id < context->function->block_count &&
            context->function->blocks[context->block_id].has_terminator) {
            body_terminated = true;
        } else {
            body_terminated = false;
            status = minic_core_lower_internal_set_branch(context, context->block_id, statement->span, condition_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    } else {
        if (!body_terminated && for_update != NULL) {
            status = minic_core_lower_internal_lower_expression_statement(context, for_update);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (context->block_id < context->function->block_count &&
                context->function->blocks[context->block_id].has_terminator) {
                body_terminated = true;
            }
        }
        if (!body_terminated) {
            status = minic_core_lower_internal_set_branch(context, context->block_id, statement->span, condition_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    }
    context->block_id = exit_block;
    /* M133_CONSTANT_TRUE_LOOP_REACHABILITY: reachability, not syntax spelling,
       owns loop fallthrough. Normal variable/false-capable conditions already
       contribute a false edge; omitted and constant-true conditions do not. */
    if (!minic_core_lower_internal_block_has_predecessor(context->function, exit_block)) {
        MinicCoreTerminator exit_terminator;

        (void)memset(&exit_terminator, 0, sizeof(exit_terminator));
        exit_terminator.kind = MINIC_CORE_TERMINATOR_UNREACHABLE;
        exit_terminator.span = statement->span;
        exit_terminator.return_value = MINIC_CORE_VALUE_INVALID;
        exit_terminator.return_object = MINIC_CORE_OBJECT_INVALID;
        if (!minic_core_function_set_terminator(
                context->function, exit_block, &exit_terminator)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *terminated = true;
        return MINIC_CORE_LOWER_OK;
    }
    *terminated = false;
    return MINIC_CORE_LOWER_OK;
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


/* M190_LOOP_INVARIANT_LOCAL_FACTS */
static bool core_expression_may_modify_local(const MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicLocalId local_id,
                                             unsigned int depth);

static bool core_direct_local_lvalue(const MinicCoreLowerContext *context,
                                     MinicExpressionId expression_id,
                                     MinicLocalId *local_id) {
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        local_id == NULL || expression_id == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
        expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    *local_id = expression->value.local_id;
    return true;
}

static bool core_block_may_modify_local(const MinicCoreLowerContext *context,
                                        const MinicBlock *block,
                                        MinicLocalId local_id,
                                        unsigned int depth) {
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        block == NULL || depth > 64U) {
        return true;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        const MinicStatement *statement = minic_c0_program_statement(
            context->body->program, block->statements[index]);
        MinicLocalId target_local;
        const MinicBlock *child;

        if (statement == NULL ||
            statement->cleanup_context != statement->cleanup_stop_context) {
            return true;
        }
        switch (statement->kind) {
        case MINIC_STATEMENT_ASSIGN:
            if (core_direct_local_lvalue(context, statement->target_expression, &target_local) &&
                target_local == local_id) {
                return true;
            }
            if (core_expression_may_modify_local(
                    context, statement->expression, local_id, depth + 1U)) {
                return true;
            }
            break;
        case MINIC_STATEMENT_RECORD_COPY:
        case MINIC_STATEMENT_RECORD_INITIALIZE:
            if (core_direct_local_lvalue(context, statement->target_expression, &target_local) &&
                target_local == local_id) {
                return true;
            }
            if (core_expression_may_modify_local(
                    context, statement->expression, local_id, depth + 1U)) {
                return true;
            }
            break;
        case MINIC_STATEMENT_EXPRESSION:
        case MINIC_STATEMENT_RETURN:
            if (statement->expression != MINIC_EXPRESSION_INVALID &&
                core_expression_may_modify_local(
                    context, statement->expression, local_id, depth + 1U)) {
                return true;
            }
            break;
        case MINIC_STATEMENT_IF:
        case MINIC_STATEMENT_WHILE:
        case MINIC_STATEMENT_SWITCH:
            if (statement->expression != MINIC_EXPRESSION_INVALID &&
                core_expression_may_modify_local(
                    context, statement->expression, local_id, depth + 1U)) {
                return true;
            }
            if (statement->then_block != MINIC_BLOCK_INVALID) {
                child = minic_c0_program_block(context->body->program, statement->then_block);
                if (child == NULL || core_block_may_modify_local(
                                         context, child, local_id, depth + 1U)) {
                    return true;
                }
            }
            if (statement->else_block != MINIC_BLOCK_INVALID) {
                child = minic_c0_program_block(context->body->program, statement->else_block);
                if (child == NULL || core_block_may_modify_local(
                                         context, child, local_id, depth + 1U)) {
                    return true;
                }
            }
            break;
        case MINIC_STATEMENT_INLINE_ASM:
            return true;
        case MINIC_STATEMENT_GOTO:
            if (statement->expression != MINIC_EXPRESSION_INVALID &&
                core_expression_may_modify_local(
                    context, statement->expression, local_id, depth + 1U)) {
                return true;
            }
            break;
        case MINIC_STATEMENT_CASE:
            if (statement->expression != MINIC_EXPRESSION_INVALID &&
                core_expression_may_modify_local(
                    context, statement->expression, local_id, depth + 1U)) {
                return true;
            }
            if (statement->target_expression != MINIC_EXPRESSION_INVALID &&
                core_expression_may_modify_local(
                    context, statement->target_expression, local_id, depth + 1U)) {
                return true;
            }
            break;
        case MINIC_STATEMENT_BREAK:
        case MINIC_STATEMENT_LABEL:
        case MINIC_STATEMENT_DEFAULT:
            break;
        default:
            return true;
        }
    }
    return false;
}

static bool core_expression_may_modify_local(const MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicLocalId local_id,
                                             unsigned int depth) {
    const MinicExpression *expression;
    MinicLocalId target_local;
    size_t argument_index;
    const MinicBlock *block;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        expression_id == MINIC_EXPRESSION_INVALID || depth > 64U) {
        return true;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return true;
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
    case MINIC_EXPRESSION_SIZEOF:
    case MINIC_EXPRESSION_OFFSETOF:
        return false;
    case MINIC_EXPRESSION_ADDRESS_OF:
        if (core_direct_local_lvalue(context, expression->value.unary.operand, &target_local) &&
            target_local == local_id) {
            return true;
        }
        return core_expression_may_modify_local(
            context, expression->value.unary.operand, local_id, depth + 1U);
    case MINIC_EXPRESSION_DEREFERENCE:
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_DISCARD:
    case MINIC_EXPRESSION_LVALUE_READ:
        return core_expression_may_modify_local(
            context, expression->value.unary.operand, local_id, depth + 1U);
    case MINIC_EXPRESSION_UNARY:
        if (expression->value.unary.operator_kind > MINIC_UNARY_BITWISE_NOT &&
            core_direct_local_lvalue(
                context, expression->value.unary.operand, &target_local) &&
            target_local == local_id) {
            return true;
        }
        return core_expression_may_modify_local(
            context, expression->value.unary.operand, local_id, depth + 1U);
    case MINIC_EXPRESSION_ASSIGNMENT:
    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT:
        if (core_direct_local_lvalue(
                context, expression->value.binary.left, &target_local) &&
            target_local == local_id) {
            return true;
        }
        return core_expression_may_modify_local(
                   context, expression->value.binary.left, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.binary.right, local_id, depth + 1U);
    case MINIC_EXPRESSION_BINARY:
        return core_expression_may_modify_local(
                   context, expression->value.binary.left, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.binary.right, local_id, depth + 1U);
    case MINIC_EXPRESSION_CONDITIONAL:
        return core_expression_may_modify_local(
                   context, expression->value.conditional.condition, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.conditional.when_true, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.conditional.when_false, local_id, depth + 1U);
    case MINIC_EXPRESSION_CALL:
        if (expression->value.call.callee != MINIC_EXPRESSION_INVALID &&
            core_expression_may_modify_local(
                context, expression->value.call.callee, local_id, depth + 1U)) {
            return true;
        }
        for (argument_index = 0U;
             argument_index < expression->value.call.argument_count;
             ++argument_index) {
            if (core_expression_may_modify_local(
                    context,
                    minic_c0_call_argument(expression, argument_index),
                    local_id,
                    depth + 1U)) {
                return true;
            }
        }
        return false;
    case MINIC_EXPRESSION_SUBSCRIPT:
        return core_expression_may_modify_local(
                   context, expression->value.subscript.base, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.subscript.index, local_id, depth + 1U);
    case MINIC_EXPRESSION_MEMBER:
        return core_expression_may_modify_local(
            context, expression->value.member.base, local_id, depth + 1U);
    case MINIC_EXPRESSION_STATEMENT:
        block = minic_c0_program_block(
            context->body->program, expression->value.statement_expression.block);
        return block == NULL ||
               core_block_may_modify_local(context, block, local_id, depth + 1U);
    case MINIC_EXPRESSION_BUILTIN_UNARY:
        return core_expression_may_modify_local(
            context, expression->value.builtin_unary.operand, local_id, depth + 1U);
    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:
        return core_expression_may_modify_local(
                   context, expression->value.overflow.left, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.overflow.right, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.overflow.result_pointer, local_id, depth + 1U);
    case MINIC_EXPRESSION_COMPOUND_LITERAL:
    case MINIC_EXPRESSION_BUILTIN_UNREACHABLE:
    case MINIC_EXPRESSION_BUILTIN_ALLOCA:
    case MINIC_EXPRESSION_BUILTIN_VA_START:
    case MINIC_EXPRESSION_BUILTIN_VA_COPY:
    case MINIC_EXPRESSION_BUILTIN_VA_END:
    case MINIC_EXPRESSION_BUILTIN_VA_ARG:
    default:
        return true;
    }
}

/* MUTABLE_SPECIALIZATION_LOOP_BARRIER: do not substitute an incoming
   specialized parameter constant for a variable updated within the loop. */
static void core_invalidate_loop_mutated_specialization_facts(
    MinicCoreLowerContext *context, const MinicStatement *loop_statement) {
    const MinicBlock *body;
    size_t index;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || context->body == NULL ||
        loop_statement == NULL || loop_statement->kind != MINIC_STATEMENT_WHILE) {
        return;
    }
    body = minic_c0_program_block(context->body->program, loop_statement->then_block);
    for (index = 0U; index < context->source_function->local_count; ++index) {
        MinicLocalId local_id = context->source_function->local_begin + index;
        const MinicCoreLocalIntegerConstant *fact =
            &context->local_integer_constants[index];
        bool may_mutate;
        if (!fact->specialization_known || fact->specialization_modified) {
            continue;
        }
        may_mutate =
            body == NULL ||
            (loop_statement->expression != MINIC_EXPRESSION_INVALID &&
             core_expression_may_modify_local(
                 context, loop_statement->expression, local_id, 0U)) ||
            core_block_may_modify_local(context, body, local_id, 0U);
        if (may_mutate) {
            minic_core_local_fact_invalidate(context, local_id);
            if (getenv("MINIC_LOOP_MUTATED_FACT_TRACE") != NULL) {
                (void)fprintf(stderr,
                              "LOOP_SPECIALIZATION_FACT_KILL function=%s local=%zu\n",
                              context->source_function->name != NULL
                                  ? context->source_function->name : "?",
                              (size_t)local_id);
            }
        }
    }
}

static void core_restore_loop_invariant_facts(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *incoming,
    const MinicStatement *loop_statement) {
    const MinicBlock *body;
    size_t index;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || incoming == NULL ||
        loop_statement == NULL || loop_statement->kind != MINIC_STATEMENT_WHILE) {
        return;
    }
    body = minic_c0_program_block(context->body->program, loop_statement->then_block);
    if (body == NULL) {
        return;
    }
    for (index = 0U; index < context->source_function->local_count; ++index) {
        MinicLocalId local_id = context->source_function->local_begin + index;
        if (!incoming[index].known || incoming[index].escaped ||
            context->local_integer_constants[index].escaped ||
            (loop_statement->expression != MINIC_EXPRESSION_INVALID &&
             core_expression_may_modify_local(
                 context, loop_statement->expression, local_id, 0U)) ||
            core_block_may_modify_local(context, body, local_id, 0U)) {
            continue;
        }
        context->local_integer_constants[index] = incoming[index];
    }
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
                        status = lower_while(
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
                status = lower_while(
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
           A plain ASSIGN has no control edge of its own, so crossing cleanup is
           still owned by the eventual RETURN/BREAK/GOTO/scope-exit edge. An
           adjacent parser-internal loop label is likewise target metadata, not
           an executable cleanup edge. Keep every other nonzero-distance shape
           fail-closed; in particular BREAK/GOTO and structured IF/WHILE are not
           generalized here. */
        if (statement->cleanup_context != statement->cleanup_stop_context &&
            statement->kind != MINIC_STATEMENT_RETURN &&
            statement->kind != MINIC_STATEMENT_ASSIGN &&
            statement->kind != MINIC_STATEMENT_BREAK &&
            !(statement->kind == MINIC_STATEMENT_GOTO &&
              statement->expression == MINIC_EXPRESSION_INVALID &&
              statement->target_statement != MINIC_STATEMENT_INVALID) &&
            !(statement->kind == MINIC_STATEMENT_LABEL &&
              statement_index + 1U < source_block->statement_count &&
              minic_core_loop_internal_while_label_pair(
                  statement,
                  minic_c0_program_statement(
                      context->body->program,
                      source_block->statements[statement_index + 1U])))) {
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
                status = lower_while(context,
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
                status = lower_record_copy_statement(context, statement);
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
                status = lower_if(context, statement, &statement_terminated);
                break;
            case MINIC_STATEMENT_WHILE: {
                const MinicBlock *local_fact_loop_body;
                MinicBlock local_fact_single_iteration_body;
                MinicStatementId local_fact_continue_label;
                MinicCoreLocalIntegerConstant *loop_incoming_facts;
                bool local_fact_single_pass;

                loop_incoming_facts = minic_core_local_facts_snapshot(context);
                if (context->source_function != NULL &&
                    context->source_function->local_count != 0U &&
                    loop_incoming_facts == NULL) {
                    return MINIC_CORE_LOWER_ERROR;
                }

                local_fact_loop_body = minic_c0_program_block(
                    context->body->program, statement->then_block);
                local_fact_continue_label = MINIC_STATEMENT_INVALID;
                local_fact_single_pass =
                    local_fact_loop_body != NULL &&
                    minic_core_loop_normalized_do_while_zero_body(context,
                                                  statement,
                                                  local_fact_loop_body,
                                                  &local_fact_single_iteration_body,
                                                  &local_fact_continue_label) &&
                    !minic_core_loop_normalized_do_while_block_needs_exit(context,
                                                          &local_fact_single_iteration_body,
                                                          local_fact_continue_label,
                                                          true);
                if (!local_fact_single_pass) {
                    /* Loop-body changes invalidate specialized parameters
                       before the initial condition and later backedges. */
                    core_invalidate_loop_mutated_specialization_facts(context, statement);
                    minic_core_local_facts_clear_known(context);
                    core_restore_loop_invariant_facts(
                        context, loop_incoming_facts, statement);
                }
                status = lower_while(
                    context, statement, MINIC_STATEMENT_INVALID, &statement_terminated);
                if (!local_fact_single_pass) {
                    minic_core_local_facts_clear_known(context);
                    core_restore_loop_invariant_facts(
                        context, loop_incoming_facts, statement);
                }
                free(loop_incoming_facts);
                break;
            }
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
        !minic_core_function_add_block(&lowered, &block_id)) {
        free(statement_blocks); free(local_integer_constants); free(local_objects); minic_core_function_destroy(&lowered); return MINIC_CORE_LOWER_ERROR;
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
