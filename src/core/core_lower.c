#include "core/core_lower_internal.h"

#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Core lowering consumes the selected target's read-only DataLayout.
 * Never bypass TargetInfo through the process-wide default target: that would
 * make the AST -> Core seam implicitly RV64-specific. */
static MinicCoreLowerStatus lower_assignment_pair(MinicCoreLowerContext *context,
                                                  MinicExpressionId target_id,
                                                  MinicExpressionId source_id,
                                                  MinicSourceSpan span,
                                                  MinicCoreValueId *result_value);
static MinicCoreLowerStatus lower_expression_statement(
    MinicCoreLowerContext *context, const MinicStatement *statement);
static MinicCoreLowerStatus
lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated);
static MinicCoreLowerStatus set_branch(MinicCoreLowerContext *context,
                                       MinicCoreBlockId block_id,
                                       MinicSourceSpan span,
                                       MinicCoreBlockId target);
static MinicCoreLowerStatus lower_condition_branch(MinicCoreLowerContext *context,
                                                   MinicExpressionId expression_id,
                                                   MinicSourceSpan span,
                                                   MinicCoreBlockId when_true,
                                                   MinicCoreBlockId when_false);
static MinicCoreLowerStatus lower_scalar_update(MinicCoreLowerContext *context,
                                                const MinicExpression *expression,
                                                MinicCoreValueId *value_id);
static MinicCoreLowerStatus spill_scalar_value(MinicCoreLowerContext *context,
                                               MinicSourceSpan span,
                                               MinicType type,
                                               MinicCoreValueId value_id,
                                               MinicCoreObjectId *object_id);
static MinicCoreLowerStatus reload_scalar_value(MinicCoreLowerContext *context,
                                                MinicSourceSpan span,
                                                MinicType type,
                                                MinicCoreObjectId object_id,
                                                MinicCoreValueId *value_id);
static MinicCoreLowerStatus lower_direct_record_call_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *result_object);
static MinicCoreLowerStatus lower_record_compound_literal_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *object_id);
static MinicCoreLowerStatus lower_record_conditional_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *result_object);
static MinicCoreLowerStatus lower_record_materialized_address(
    MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicCoreValueId *address_id);
static MinicCoreLowerStatus append_integer_conversion(MinicCoreLowerContext *context,
                                                      MinicSourceSpan span,
                                                      MinicType target_type,
                                                      MinicCoreValueId source_value,
                                                      MinicCoreValueId *value_id);
static MinicCoreLowerStatus lower_scalar_assignment_value(MinicCoreLowerContext *context,
                                                          MinicType target_type,
                                                          MinicExpressionId expression_id,
                                                          MinicCoreValueId *value_id);

MinicCoreLowerStatus ensure_statement_block(MinicCoreLowerContext *context, MinicStatementId statement_id, MinicCoreBlockId *block_id) {
    MinicCoreBlockId mapped;
    if (context == NULL || context->function == NULL || block_id == NULL || context->statement_blocks == NULL || statement_id >= context->statement_block_count) return MINIC_CORE_LOWER_ERROR;
    mapped = context->statement_blocks[statement_id];
    if (mapped == MINIC_CORE_BLOCK_INVALID) {
        if (!minic_core_function_add_block(context->function, &mapped)) return MINIC_CORE_LOWER_ERROR;
        context->statement_blocks[statement_id] = mapped;
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
        if (minic_type_is_volatile(parameter->type) || parameter->is_array ||
            parameter->is_register_storage ||
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
            instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
            instruction.span = parameter->name_span;
            instruction.type = parameter_value_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.parameter_index = parameter_index;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &parameter_value)) {
                return MINIC_CORE_LOWER_ERROR;
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
            instruction.value.store.is_volatile = false;
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &instruction)) {
                return MINIC_CORE_LOWER_ERROR;
            }
        }
    }
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus append_field_address(MinicCoreLowerContext *context,
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
            status = lower_block(context, initializer_block, &terminated);
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
        status = lower_block(context, initializer_block, &terminated);
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
            spill_scalar_value(context, base->span, pointer_type, base_value, &base_object);
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
            subscript_status = append_integer_conversion(
                context, index->span, index_value_type, index_value, &index_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
        }
        subscript_status =
            reload_scalar_value(context, base->span, pointer_type, base_object, &base_value);
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
        return append_field_address(context,
                                    expression->span,
                                    base_id,
                                    expression->value.member.record_id,
                                    expression->value.member.field_index,
                                    expression->type,
                                    address_id);
    }
    return MINIC_CORE_LOWER_UNSUPPORTED;
}

static MinicCoreLowerStatus append_integer_conversion(MinicCoreLowerContext *context,
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

static MinicCoreLowerStatus spill_scalar_value(MinicCoreLowerContext *context,
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

static MinicCoreLowerStatus store_scalar_value(MinicCoreLowerContext *context,
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

static MinicCoreLowerStatus reload_scalar_value(MinicCoreLowerContext *context,
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

static MinicCoreLowerStatus lower_scalar_equality_operands(MinicCoreLowerContext *context,
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
        status = append_integer_conversion(
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
    status = spill_scalar_value(
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
        status = append_integer_conversion(
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
    status = reload_scalar_value(
        context, left_expression->span, comparison_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    *right_value = right_normalized;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_integer_binary_operands(MinicCoreLowerContext *context,
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
    status = append_integer_conversion(
        context, left_expression->span, result_type, left_source, &left_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-convert status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    status = spill_scalar_value(
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
    status = append_integer_conversion(
        context, right_expression->span, result_type, right_source, &right_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=right-convert status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    status =
        reload_scalar_value(context, left_expression->span, result_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-reload status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    *right_value = right_normalized;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_double_binary_operands(MinicCoreLowerContext *context,
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
        !minic_type_is_double(result_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    left_expression = minic_c0_program_expression(context->body->program, left_id);
    right_expression = minic_c0_program_expression(context->body->program, right_id);
    if (left_expression == NULL || right_expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    status = lower_scalar_assignment_value(context, result_type, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = spill_scalar_value(
        context, left_expression->span, result_type, left_source, &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = lower_scalar_assignment_value(context, result_type, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = reload_scalar_value(
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
    status = lower_block(context, initializer_block, &terminated);
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
        status = lower_record_materialized_address(
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
        status = lower_block(context, block, &terminated);
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
        return lower_record_materialized_address(
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

static MinicCoreLowerStatus lower_record_materialized_address(
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
        status = lower_block(context, statement_block, &terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return lower_record_materialized_address(
            context, expression->value.statement_expression.result, address_id);
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_ARG) {
        status = lower_record_va_arg_object(context, expression, &object_id);
    } else if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {
        status = lower_record_conditional_object(context, expression, &object_id);
    } else if (expression->kind == MINIC_EXPRESSION_CALL &&
               expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
        status = lower_direct_record_call_object(context, expression, &object_id);
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
    status = lower_condition_branch(context,
                                    expression->value.conditional.condition,
                                    expression->span,
                                    true_block,
                                    false_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = true_block;
    status = lower_record_materialized_address(
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
    status = set_branch(context, context->block_id, expression->span, merge_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = false_block;
    status = lower_record_materialized_address(
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
    status = set_branch(context, context->block_id, expression->span, merge_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = merge_block;
    return MINIC_CORE_LOWER_OK;
}

/* BATCH_M_RECORD_LOAD: turn an address-backed record rvalue/lvalue wrapper
   into a private Core snapshot object.  The source pointer keeps its qualifiers
   so volatile aggregate reads remain explicit at the IR boundary. */
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
        !minic_c0_record_value_is_address_backed(context->body->program, expression_id) ||
        !minic_type_unqualified(expression->type, &expression_type) ||
        !minic_type_is_record(expression_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_record_value_address(context, expression_id, &source_address);
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

        status = lower_direct_record_call_object(context, source, &source_object);
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
        status = lower_record_materialized_address(
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
    return append_integer_conversion(
        context, expression->span, result_type, source_value, value_id);
}

static MinicCoreLowerStatus lower_scalar_assignment_value(MinicCoreLowerContext *context,
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
    if (expression == NULL ||
        !minic_c0_assignment_compatible(context->body->program, target_type, expression_id)) {
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
    if (minic_type_is_double(target_type)) {
        MinicType source_type;

        if (!core_scalar_expression_value_type(context->body, expression, &source_type) ||
            !minic_type_is_double(source_type) ||
            !minic_type_equal(source_type, target_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_type_equal(context->function->values[source_value].type, source_type)) {
            return MINIC_CORE_LOWER_ERROR;
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
    return append_integer_conversion(context, expression->span, target_type, truth_value, value_id);
}

/* M85_RECORD_CALL_ARGUMENT: materialize a by-value record argument as a
   private Core object snapshot before later arguments are evaluated. */
static MinicCoreLowerStatus lower_record_call_argument_object(
    MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicType parameter_type,
    MinicCoreObjectId *object_id) {
    /* M128_RECORD_CALL_ARGUMENT_MATERIALIZATION: a by-value record argument
       consumes a record rvalue, not merely an already-address-backed lvalue.
       Make lower_record_materialized_address() the single aggregate producer
       owner here: it handles conditionals, compound literals, direct record
       returns, and falls back fail-closed for ordinary address-backed values.
       The private argument object below remains the evaluation-order snapshot
       consumed by the existing Core/ABI OBJECT call-argument path. */
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
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
        bool found;

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
        field_index = 0U;
        found = false;
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *candidate;

            candidate = minic_c0_record_field(record, field_index);
            if (candidate == NULL || candidate->is_array || candidate->is_bit_field ||
                !minic_type_is_pointer(candidate->type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            if (minic_c0_assignment_compatible(
                    context->body->program, candidate->type, expression_id)) {
                field = candidate;
                found = true;
                break;
            }
        }
        if (!found || field == NULL) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_scalar_assignment_value(
            context, field->type, expression_id, &field_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, parameter_type, object_id) ||
            !minic_type_pointer_to(parameter_type, &pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = *object_id;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &object_address)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = append_field_address(context,
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
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = field_address;
        instruction.value.store.stored_value = field_value;
        instruction.value.store.is_volatile = false;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
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
        return lower_direct_record_call_object(context, expression, object_id);
    }
    status = lower_record_materialized_address(context, expression_id, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (!minic_core_function_add_object(context->function, expression->span, parameter_type, object_id) ||
        !minic_type_pointer_to(parameter_type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = expression->span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = *object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &destination_address)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    instruction.span = expression->span;
    instruction.type = parameter_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.record_copy.destination_address = destination_address;
    instruction.value.record_copy.source_address = source_address;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,
                                              const MinicExpression *expression,
                                              MinicCoreValueId *value_id) {
    const MinicFunction *callee;
    const char *callee_name;
    size_t callee_name_length;
    MinicCoreCalleeId callee_id;
    MinicCoreInstruction instruction;
    MinicCoreCallArgument *arguments;
    MinicCoreObjectId argument_objects[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicType argument_types[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicCoreLowerStatus status;
    size_t argument_begin;
    size_t argument_count;
    size_t argument_index;
    bool returns_void;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || value_id == NULL ||
        expression->kind != MINIC_EXPRESSION_CALL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    callee = minic_c0_program_function(context->body->program, expression->value.call.function_id);
    if (callee == NULL || callee->name == NULL || callee->name_length == 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    callee_name = callee->assembler_name != NULL ? callee->assembler_name : callee->name;
    callee_name_length =
        callee->assembler_name != NULL ? callee->assembler_name_length : callee->name_length;
    if (callee_name == NULL || callee_name_length == 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    argument_count = expression->value.call.argument_count;
    returns_void = minic_type_is_void(callee->return_type);
    if (argument_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (!callee->is_variadic && argument_count != callee->parameter_count) ||
        (callee->is_variadic && argument_count < callee->parameter_count) ||
        (!returns_void && !core_memory_scalar_type(callee->return_type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
        if (argument_index < callee->parameter_count) {
            argument_types[argument_index] = callee->parameter_types[argument_index];
            if (!core_memory_scalar_type(argument_types[argument_index]) &&
                !minic_type_is_record(argument_types[argument_index])) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        } else {
            const MinicExpression *argument_expression = minic_c0_program_expression(
                context->body->program, expression->value.call.arguments[argument_index]);
            if (argument_expression == NULL) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            if (minic_type_is_record(argument_expression->type)) {
                if (!minic_type_unqualified(
                        argument_expression->type, &argument_types[argument_index]) ||
                    !minic_type_is_record(argument_types[argument_index])) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
            } else if (!core_scalar_expression_value_type(
                           context->body, argument_expression, &argument_types[argument_index]) ||
                       !core_memory_scalar_type(argument_types[argument_index])) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        }
    }
    arguments = argument_count == 0U
                    ? NULL
                    : (MinicCoreCallArgument *)calloc(argument_count, sizeof(*arguments));
    if (argument_count != 0U && arguments == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
        if (minic_type_is_record(argument_types[argument_index])) {
            MinicCoreObjectId object_id;

            status = lower_record_call_argument_object(
                context,
                expression->value.call.arguments[argument_index],
                argument_types[argument_index],
                &object_id);
            if (status != MINIC_CORE_LOWER_OK) {
                free(arguments);
                return status;
            }
            arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
            arguments[argument_index].value.object_id = object_id;
            continue;
        }
        arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
        if (argument_index < callee->parameter_count) {
            status = lower_scalar_assignment_value(
                context,
                argument_types[argument_index],
                expression->value.call.arguments[argument_index],
                &arguments[argument_index].value.value_id);
        } else {
            status = lower_expression(context,
                                      expression->value.call.arguments[argument_index],
                                      &arguments[argument_index].value.value_id);
        }
        if (status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr,
                          "CORE_LOWER_DETAIL marker=BATCH_D_VARIADIC_DIRECT_CALL function=%s "
                          "stage=direct-call callee=%s arg=%zu fixed=%d reason=argument-lower status=%d\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          callee_name,
                          argument_index,
                          argument_index < callee->parameter_count ? 1 : 0,
                          (int)status);
            free(arguments);
            return status;
        }
        if (arguments[argument_index].value.value_id >= context->function->value_count ||
            !minic_type_equal(
                context->function->values[arguments[argument_index].value.value_id].type,
                argument_types[argument_index])) {
            free(arguments);
            return MINIC_CORE_LOWER_ERROR;
        }
        status = spill_scalar_value(context,
                                    expression->span,
                                    argument_types[argument_index],
                                    arguments[argument_index].value.value_id,
                                    &argument_objects[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
        if (arguments[argument_index].kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            continue;
        }
        status = reload_scalar_value(context,
                                     expression->span,
                                     argument_types[argument_index],
                                     argument_objects[argument_index],
                                     &arguments[argument_index].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    if (!minic_core_function_add_callee(context->function,
                                        callee_name,
                                        callee_name_length,
                                        callee->return_type,
                                        callee->parameter_types,
                                        callee->parameter_count,
                                        callee->is_variadic,
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
    instruction.type = callee->return_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.call.callee_id = callee_id;
    instruction.value.call.argument_begin = argument_begin;
    instruction.value.call.argument_count = argument_count;
    instruction.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (returns_void) {
        *value_id = MINIC_CORE_VALUE_INVALID;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

/* M86_DIRECT_RECORD_CALL_RESULT: direct record returns are materialized into
   private Core objects. Arguments keep the M85 VALUE/OBJECT representation and
   the RV64 backend remains the sole owner of ABI register placement. */
static MinicCoreLowerStatus lower_direct_record_call_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *result_object) {
    const MinicFunction *callee;
    const char *callee_name;
    size_t callee_name_length;
    MinicCoreCalleeId callee_id;
    MinicCoreInstruction instruction;
    MinicCoreCallArgument *arguments;
    MinicCoreObjectId argument_objects[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicCoreLowerStatus status;
    size_t argument_begin;
    size_t argument_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || result_object == NULL ||
        expression->kind != MINIC_EXPRESSION_CALL ||
        expression->value.call.function_id == MINIC_FUNCTION_INVALID ||
        !minic_type_is_record(expression->type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    callee = minic_c0_program_function(context->body->program, expression->value.call.function_id);
    if (callee == NULL || callee->name == NULL || callee->name_length == 0U ||
        !minic_type_is_record(callee->return_type) ||
        !minic_type_equal(callee->return_type, expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    callee_name = callee->assembler_name != NULL ? callee->assembler_name : callee->name;
    callee_name_length =
        callee->assembler_name != NULL ? callee->assembler_name_length : callee->name_length;
    if (callee_name == NULL || callee_name_length == 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (callee->is_variadic || expression->value.call.argument_count != callee->parameter_count) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        if (!core_memory_scalar_type(callee->parameter_types[argument_index]) &&
            !minic_type_is_record(callee->parameter_types[argument_index])) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    }
    arguments = callee->parameter_count == 0U
                    ? NULL
                    : (MinicCoreCallArgument *)calloc(callee->parameter_count, sizeof(*arguments));
    if (callee->parameter_count != 0U && arguments == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        if (minic_type_is_record(callee->parameter_types[argument_index])) {
            MinicCoreObjectId object_id;

            status = lower_record_call_argument_object(
                context,
                expression->value.call.arguments[argument_index],
                callee->parameter_types[argument_index],
                &object_id);
            if (status != MINIC_CORE_LOWER_OK) {
                free(arguments);
                return status;
            }
            arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
            arguments[argument_index].value.object_id = object_id;
            continue;
        }
        arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
        status = lower_scalar_assignment_value(
            context,
            callee->parameter_types[argument_index],
            expression->value.call.arguments[argument_index],
            &arguments[argument_index].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
        if (arguments[argument_index].value.value_id >= context->function->value_count ||
            !minic_type_equal(
                context->function->values[arguments[argument_index].value.value_id].type,
                callee->parameter_types[argument_index])) {
            free(arguments);
            return MINIC_CORE_LOWER_ERROR;
        }
        status = spill_scalar_value(context,
                                    expression->span,
                                    callee->parameter_types[argument_index],
                                    arguments[argument_index].value.value_id,
                                    &argument_objects[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        if (arguments[argument_index].kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            continue;
        }
        status = reload_scalar_value(context,
                                     expression->span,
                                     callee->parameter_types[argument_index],
                                     argument_objects[argument_index],
                                     &arguments[argument_index].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    if (!minic_core_function_add_callee(context->function,
                                        callee_name,
                                        callee_name_length,
                                        callee->return_type,
                                        callee->parameter_types,
                                        callee->parameter_count,
                                        callee->is_variadic,
                                        &callee_id) ||
        !minic_core_function_append_call_arguments(
            context->function, arguments, callee->parameter_count, &argument_begin)) {
        free(arguments);
        return MINIC_CORE_LOWER_ERROR;
    }
    free(arguments);
    if (!minic_core_function_add_object(
            context->function, expression->span, callee->return_type, result_object)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_CALL;
    instruction.span = expression->span;
    instruction.type = callee->return_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.call.callee_id = callee_id;
    instruction.value.call.argument_begin = argument_begin;
    instruction.value.call.argument_count = callee->parameter_count;
    instruction.value.call.result_object = *result_object;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

/* M83_FIRST_CLASS_INDIRECT_CALL: keep the callee as a first-class SSA
   function-pointer value; the static signature is copied into Core so
   verification and later backends do not depend on frontend Program state. */
static MinicCoreLowerStatus lower_indirect_call(MinicCoreLowerContext *context,
                                                const MinicExpression *expression,
                                                MinicCoreValueId *value_id) {
    const MinicExpression *callee_expression;
    const MinicFunctionType *signature;
    MinicCoreCallSignatureId signature_id;
    MinicCoreInstruction instruction;
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

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || value_id == NULL ||
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
    if (!returns_void && !core_memory_scalar_type(signature->return_type)) {
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
        if (argument_index < signature->parameter_count) {
            argument_types[argument_index] = signature->parameter_types[argument_index];
            if (minic_type_is_record(argument_types[argument_index])) {
                MinicCoreObjectId object_id;

                status = lower_record_call_argument_object(
                    context,
                    expression->value.call.arguments[argument_index],
                    argument_types[argument_index],
                    &object_id);
                if (status != MINIC_CORE_LOWER_OK) {
                    free(arguments);
                    return status;
                }
                arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
                arguments[argument_index].value.object_id = object_id;
                continue;
            }
            if (!core_memory_scalar_type(argument_types[argument_index])) {
                free(arguments);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
            status = lower_scalar_assignment_value(
                context,
                argument_types[argument_index],
                expression->value.call.arguments[argument_index],
                &arguments[argument_index].value.value_id);
        } else {
            const MinicExpression *argument_expression = minic_c0_program_expression(
                context->body->program, expression->value.call.arguments[argument_index]);

            if (argument_expression == NULL ||
                !core_scalar_expression_value_type(
                    context->body, argument_expression, &argument_types[argument_index]) ||
                !core_memory_scalar_type(argument_types[argument_index])) {
                free(arguments);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
            status = lower_expression(context,
                                      expression->value.call.arguments[argument_index],
                                      &arguments[argument_index].value.value_id);
        }
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
        if (arguments[argument_index].value.value_id >= context->function->value_count ||
            !minic_type_equal(
                context->function->values[arguments[argument_index].value.value_id].type,
                argument_types[argument_index])) {
            free(arguments);
            return MINIC_CORE_LOWER_ERROR;
        }
        status = spill_scalar_value(context,
                                    expression->span,
                                    argument_types[argument_index],
                                    arguments[argument_index].value.value_id,
                                    &argument_objects[argument_index]);
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
    for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
        if (arguments[argument_index].kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            continue;
        }
        status = reload_scalar_value(context,
                                     expression->span,
                                     argument_types[argument_index],
                                     argument_objects[argument_index],
                                     &arguments[argument_index].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    if (callee_value >= context->function->value_count ||
        !minic_type_equal(context->function->values[callee_value].type, callee_value_type)) {
        free(arguments);
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_core_function_add_call_signature(context->function,
                                                function_type.function_type_id,
                                                signature->return_type,
                                                signature->parameter_types,
                                                signature->parameter_count,
                                                signature->is_variadic,
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
    instruction.type = signature->return_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.indirect_call.callee = callee_value;
    instruction.value.indirect_call.signature_id = signature_id;
    instruction.value.indirect_call.argument_begin = argument_begin;
    instruction.value.indirect_call.argument_count = argument_count;
    if (returns_void) {
        *value_id = MINIC_CORE_VALUE_INVALID;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

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
    /* M103_INTEGER_BIT_FIELD_READ: a bit-field is not C-addressable, but
       reading it is a scalar operation. Form the storage-unit address
       internally, load it through an unsigned storage type, extract the field,
       then explicitly sign-extend signed fields from their declared bit width.
       Bit-field writes remain owned by their dedicated RMW lowering paths. */
    if (expression->kind == MINIC_EXPRESSION_MEMBER &&
        expression->value_category == MINIC_VALUE_LVALUE) {
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreInstruction extract;
        MinicCoreValueId address_id;
        MinicCoreValueId base_id;
        MinicCoreValueId current;
        MinicCoreValueId rhs;
        MinicCoreLowerStatus status;
        MinicType base_value_type;
        MinicType record_type;
        MinicType storage_access_type;
        MinicType storage_type;
        MinicType value_type;
        size_t byte_offset;
        size_t bit_offset;
        unsigned int storage_width;
        uint64_t mask_bits;

        base = minic_c0_program_expression(context->body->program, expression->value.member.base);
        record = minic_c0_program_record(context->body->program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (field != NULL && field->is_bit_field) {
            if (base == NULL || record == NULL || field->bit_width == 0U ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !minic_type_is_integer(value_type) ||
                !core_bit_field_storage_type(
                    context, value_type, &storage_type, &storage_width) ||
                storage_width == 0U || storage_width > 64U ||
                field->bit_width > storage_width ||
                !minic_data_layout_record_field_layout(core_data_layout(context),
                                                       context->body->program,
                                                       record,
                                                       expression->value.member.field_index,
                                                       &byte_offset,
                                                       &bit_offset) ||
                bit_offset + field->bit_width > storage_width ||
                !core_scalar_expression_value_type(context->body, base, &base_value_type) ||
                !minic_type_is_pointer(base_value_type) ||
                !minic_type_pointee(base_value_type, &record_type) ||
                !minic_type_is_record(record_type) ||
                record_type.record_id != expression->value.member.record_id) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            (void)byte_offset;
            storage_access_type = storage_type;
            if (minic_type_is_const(expression->type) &&
                !minic_type_add_const(storage_access_type, &storage_access_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (minic_type_is_volatile(expression->type) &&
                !minic_type_add_volatile(storage_access_type, &storage_access_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = lower_expression(context, expression->value.member.base, &base_id);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (base_id >= context->function->value_count ||
                !minic_type_equal(context->function->values[base_id].type, base_value_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = append_field_address(context,
                                          expression->span,
                                          base_id,
                                          expression->value.member.record_id,
                                          expression->value.member.field_index,
                                          storage_access_type,
                                          &address_id);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&extract, 0, sizeof(extract));
            extract.kind = MINIC_CORE_INSTRUCTION_LOAD;
            extract.span = expression->span;
            extract.type = storage_type;
            extract.result = MINIC_CORE_VALUE_INVALID;
            extract.value.load.address = address_id;
            extract.value.load.is_volatile = minic_type_is_volatile(expression->type);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &extract, &current)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (bit_offset != 0U) {
                (void)memset(&extract, 0, sizeof(extract));
                extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                extract.span = expression->span;
                extract.type = minic_type_unsigned_int();
                extract.result = MINIC_CORE_VALUE_INVALID;
                extract.value.integer_value = (int64_t)bit_offset;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &extract, &rhs)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&extract, 0, sizeof(extract));
                extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                extract.span = expression->span;
                extract.type = storage_type;
                extract.result = MINIC_CORE_VALUE_INVALID;
                extract.value.binary.left = current;
                extract.value.binary.right = rhs;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &extract, &current)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (field->bit_width < storage_width) {
                mask_bits = field->bit_width == 64U
                                ? UINT64_MAX
                                : ((UINT64_C(1) << field->bit_width) - UINT64_C(1));
                (void)memset(&extract, 0, sizeof(extract));
                extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                extract.span = expression->span;
                extract.type = storage_type;
                extract.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&extract.value.integer_value, &mask_bits, sizeof(mask_bits));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &extract, &rhs)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&extract, 0, sizeof(extract));
                extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
                extract.span = expression->span;
                extract.type = storage_type;
                extract.result = MINIC_CORE_VALUE_INVALID;
                extract.value.binary.left = current;
                extract.value.binary.right = rhs;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &extract, &current)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (minic_type_is_unsigned_integer(value_type)) {
                if (minic_type_equal(storage_type, value_type)) {
                    *value_id = current;
                    return MINIC_CORE_LOWER_OK;
                }
                return append_integer_conversion(
                    context, expression->span, value_type, current, value_id);
            }
            {
                MinicCoreValueId signed_current;
                unsigned int value_width;

                if (!minic_target_info_integer_width(
                        context->target, context->body->program, value_type, &value_width) ||
                    value_width != storage_width) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                status = append_integer_conversion(
                    context, expression->span, value_type, current, &signed_current);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                if (field->bit_width < storage_width) {
                    MinicCoreValueId shift;
                    uint64_t shift_bits = (uint64_t)(storage_width - field->bit_width);

                    (void)memset(&extract, 0, sizeof(extract));
                    extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                    extract.span = expression->span;
                    extract.type = minic_type_unsigned_int();
                    extract.result = MINIC_CORE_VALUE_INVALID;
                    (void)memcpy(&extract.value.integer_value, &shift_bits, sizeof(shift_bits));
                    if (!minic_core_function_append_value_instruction(
                            context->function, context->block_id, &extract, &shift)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                    (void)memset(&extract, 0, sizeof(extract));
                    extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                    extract.span = expression->span;
                    extract.type = value_type;
                    extract.result = MINIC_CORE_VALUE_INVALID;
                    extract.value.binary.left = signed_current;
                    extract.value.binary.right = shift;
                    if (!minic_core_function_append_value_instruction(
                            context->function, context->block_id, &extract, &signed_current)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                    (void)memset(&extract, 0, sizeof(extract));
                    extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                    extract.span = expression->span;
                    extract.type = value_type;
                    extract.result = MINIC_CORE_VALUE_INVALID;
                    extract.value.binary.left = signed_current;
                    extract.value.binary.right = shift;
                    if (!minic_core_function_append_value_instruction(
                            context->function, context->block_id, &extract, &signed_current)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                }
                *value_id = signed_current;
                return MINIC_CORE_LOWER_OK;
            }
        }
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
            status = lower_direct_record_call_object(context, base, &result_object);
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
            status = append_field_address(context,
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
            status = spill_scalar_value(
                context, target->span, address_type, address, &address_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = spill_scalar_value(
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
            status = reload_scalar_value(
                context, target->span, stored_type, current_object, &current);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = reload_scalar_value(
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
        return lower_assignment_pair(context,
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
            status = lower_block(context, statement_block, &terminated);
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
            status = lower_block(context, statement_block, &terminated);
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
        status = lower_block(context, statement_block, &terminated);
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
    /* M57_CONSTANT_CONDITIONAL_PRUNING: if the frontend can prove
       the condition, lower only the selected arm. Besides being smaller CFG,
       this is semantically important for GNU compile-time choice idioms: the
       dead arm may contain target builtins that are never evaluated. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        !expression->value.conditional.uses_condition_value &&
        expression->value.conditional.when_true != MINIC_EXPRESSION_INVALID &&
        expression->value.conditional.when_false != MINIC_EXPRESSION_INVALID &&
        context->target != NULL) {
        MinicConstValue condition_value;
        MinicExpressionId selected_expression;
        bool condition_is_zero;

        if (minic_const_eval_integer(context->body->program,
                                     context->target,
                                     expression->value.conditional.condition,
                                     &condition_value) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &condition_value,
                                      &condition_is_zero)) {
            selected_expression = condition_is_zero
                                      ? expression->value.conditional.when_false
                                      : expression->value.conditional.when_true;
            if (minic_type_is_void(expression->type)) {
                MinicCoreLowerStatus status;
                MinicCoreValueId discarded_value;

                status = lower_expression(context, selected_expression, &discarded_value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                if (discarded_value != MINIC_CORE_VALUE_INVALID) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                *value_id = MINIC_CORE_VALUE_INVALID;
                return MINIC_CORE_LOWER_OK;
            }
            if (!core_memory_scalar_type(expression->type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            return lower_scalar_assignment_value(
                context, expression->type, selected_expression, value_id);
        }
    }
    /* M53_VOID_CONDITIONAL_EXPRESSION: C permits an effect-only
       conditional when both arms have void type. Model it as CFG only; there is
       deliberately no synthetic scalar result or spill object. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        !expression->value.conditional.uses_condition_value &&
        expression->value.conditional.when_true != MINIC_EXPRESSION_INVALID &&
        expression->value.conditional.when_false != MINIC_EXPRESSION_INVALID &&
        minic_type_is_void(expression->type)) {
        const MinicExpression *false_expression;
        const MinicExpression *true_expression;
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        true_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.when_true);
        false_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.when_false);
        if (true_expression == NULL || false_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_type_is_void(true_expression->type) ||
            !minic_type_is_void(false_expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_condition_branch(context,
                                        expression->value.conditional.condition,
                                        expression->span,
                                        true_block,
                                        false_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = true_block;
        status = lower_expression(
            context, expression->value.conditional.when_true, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (discarded_value != MINIC_CORE_VALUE_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = false_block;
        status = lower_expression(
            context, expression->value.conditional.when_false, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (discarded_value != MINIC_CORE_VALUE_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = merge_block;
        *value_id = MINIC_CORE_VALUE_INVALID;
        return MINIC_CORE_LOWER_OK;
    }
    /* BATCH_N_GNU_OMITTED_MIDDLE_CONDITIONAL: GNU `a ?: b` reuses the
       already-evaluated condition value on the true path.  Materialize that
       converted value before branching so `a` is evaluated exactly once; the
       false path alone evaluates and overwrites with `b`. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        expression->value.conditional.uses_condition_value) {
        const MinicExpression *condition_expression;
        const MinicExpression *false_expression;
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreBlockId branch_true;
        MinicCoreBlockId branch_false;
        MinicCoreObjectId result_object;
        MinicCoreValueId condition_value;
        MinicCoreValueId true_value;
        MinicCoreValueId false_value;
        MinicCoreValueId branch_value;
        MinicCoreLowerStatus status;
        MinicCoreInstruction zero_test;
        MinicCoreTerminator terminator;
        MinicType assignment_type;
        MinicType result_type;

        if (expression->value.conditional.condition == MINIC_EXPRESSION_INVALID ||
            expression->value.conditional.when_true != expression->value.conditional.condition ||
            expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
            !core_scalar_expression_value_type(context->body, expression, &result_type) ||
            !core_memory_scalar_type(result_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        condition_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.condition);
        false_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.when_false);
        if (condition_expression == NULL || false_expression == NULL ||
            (!minic_type_is_integer(condition_expression->type) &&
             !minic_type_is_pointer(condition_expression->type)) ||
            !minic_c0_assignment_compatible(context->body->program,
                                            result_type,
                                            expression->value.conditional.condition) ||
            !minic_c0_assignment_compatible(context->body->program,
                                            result_type,
                                            expression->value.conditional.when_false)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, result_type, &result_object) ||
            !minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        status = lower_expression(
            context, expression->value.conditional.condition, &condition_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (condition_value >= context->function->value_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_integer(result_type) &&
            minic_type_is_integer(condition_expression->type)) {
            if (!minic_c0_integer_assignment_value_type(
                    context->body->program,
                    result_type,
                    expression->value.conditional.condition,
                    &assignment_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = append_integer_conversion(context,
                                               condition_expression->span,
                                               assignment_type,
                                               condition_value,
                                               &true_value);
        } else if (minic_type_is_pointer(result_type) &&
                   (minic_type_is_pointer(condition_expression->type) ||
                    minic_c0_expression_is_null_pointer_constant_v0(
                        context->body->program,
                        expression->value.conditional.condition))) {
            status = append_scalar_bitcast(context,
                                           condition_expression->span,
                                           result_type,
                                           condition_value,
                                           &true_value);
        } else {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = store_scalar_value(context,
                                    condition_expression->span,
                                    result_type,
                                    result_object,
                                    true_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        branch_value = condition_value;
        branch_true = true_block;
        branch_false = false_block;
        if (minic_type_is_pointer(condition_expression->type)) {
            MinicCoreBlockId original_true;

            if (!minic_type_is_pointer(context->function->values[branch_value].type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&zero_test, 0, sizeof(zero_test));
            zero_test.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
            zero_test.span = expression->span;
            zero_test.type = minic_type_int();
            zero_test.result = MINIC_CORE_VALUE_INVALID;
            zero_test.value.operand = branch_value;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &zero_test, &branch_value)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            original_true = branch_true;
            branch_true = branch_false;
            branch_false = original_true;
        } else if (!minic_type_is_integer(context->function->values[branch_value].type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&terminator, 0, sizeof(terminator));
        terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
        terminator.span = expression->span;
        terminator.return_value = MINIC_CORE_VALUE_INVALID;
        terminator.conditional.condition = branch_value;
        terminator.conditional.when_true = branch_true;
        terminator.conditional.when_false = branch_false;
        if (!minic_core_function_set_terminator(
                context->function, context->block_id, &terminator)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        context->block_id = true_block;
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = false_block;
        status = lower_scalar_assignment_value(context,
                                               result_type,
                                               expression->value.conditional.when_false,
                                               &false_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = store_scalar_value(context,
                                    false_expression->span,
                                    result_type,
                                    result_object,
                                    false_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = merge_block;
        return reload_scalar_value(
            context, expression->span, result_type, result_object, value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {
        const MinicExpression *false_expression;
        const MinicExpression *true_expression;
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreObjectId result_object;
        MinicCoreValueId arm_value;
        MinicCoreLowerStatus status;
        MinicType false_type;
        MinicType result_type;
        MinicType true_type;

        /* M60_POINTER_CONDITIONAL_VALUE: C conditional values may be pointer
           scalars as well as integers. The existing arm conversion, spill and
           reload machinery is already scalar-generic, so keep the semantic
           restriction at the Core scalar boundary rather than at integer-only. */
        if (expression->value.conditional.uses_condition_value ||
            expression->value.conditional.when_true == MINIC_EXPRESSION_INVALID ||
            expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
            !core_scalar_expression_value_type(context->body, expression, &result_type) ||
            !core_memory_scalar_type(result_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        true_expression = minic_c0_program_expression(context->body->program,
                                                      expression->value.conditional.when_true);
        false_expression = minic_c0_program_expression(context->body->program,
                                                       expression->value.conditional.when_false);
        if (true_expression == NULL || false_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* M55_SCALAR_CONDITIONAL_ARM_CONVERSION: the frontend owns the
           conditional result type. The selected arm undergoes the same scalar
           conversion as assignment to that type; its source type need not
           already be identical. */
        if (!core_scalar_expression_value_type(context->body, true_expression, &true_type) ||
            !core_scalar_expression_value_type(context->body, false_expression, &false_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, result_type, &result_object) ||
            !minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        status = lower_condition_branch(context,
                                        expression->value.conditional.condition,
                                        expression->span,
                                        true_block,
                                        false_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = true_block;
        status = lower_scalar_assignment_value(context,
                                               result_type,
                                               expression->value.conditional.when_true,
                                               &arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = store_scalar_value(
            context, true_expression->span, result_type, result_object, arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = false_block;
        status = lower_scalar_assignment_value(context,
                                               result_type,
                                               expression->value.conditional.when_false,
                                               &arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = store_scalar_value(
            context, false_expression->span, result_type, result_object, arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = merge_block;
        return reload_scalar_value(
            context, expression->span, result_type, result_object, value_id);
    }
    /* M73_COMMA_EXPRESSION_VALUE: the left operand is sequenced for
       side effects and its scalar value is discarded; the right operand
       supplies the value of the whole comma expression. Unsupported left
       operand forms remain fail-closed through lower_expression(). */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_COMMA) {
        const MinicExpression *discarded_expression;
        MinicStatement discarded_statement;
        MinicCoreLowerStatus status;

        discarded_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        if (discarded_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&discarded_statement, 0, sizeof(discarded_statement));
        discarded_statement.kind = MINIC_STATEMENT_EXPRESSION;
        discarded_statement.span = discarded_expression->span;
        discarded_statement.expression = expression->value.binary.left;
        status = lower_expression_statement(context, &discarded_statement);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        return lower_expression(context, expression->value.binary.right, value_id);
    }

    /* M58_LOGICAL_OR_VALUE: lower_condition_branch already owns the
       short-circuit semantics for both && and ||. Their value materialization
       is identical: branch to true/false, store 1/0, then reload. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR)) {
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreObjectId result_object;
        MinicCoreValueId address_value;
        MinicCoreValueId constant_value;
        MinicCoreLowerStatus status;
        MinicType result_pointer_type;

        if (!minic_type_equal(expression->type, minic_type_int()) ||
            !minic_type_pointer_to(minic_type_int(), &result_pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, minic_type_int(), &result_object) ||
            !minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        status = lower_condition_branch(
            context, expression_id, expression->span, true_block, false_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = false_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = 0;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = result_pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = result_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &address_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = address_value;
        instruction.value.store.stored_value = constant_value;
        instruction.value.store.is_volatile = false;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = true_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = 1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = result_pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = result_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &address_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = address_value;
        instruction.value.store.stored_value = constant_value;
        instruction.value.store.is_volatile = false;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = merge_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = result_pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = result_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &address_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.load.address = address_value;
        instruction.value.load.is_volatile = false;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
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
        MinicCoreLowerStatus status;

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
    /* M129_LEAF_EXPRESSION_OWNERS: __builtin_isdigit is a pure integer leaf.
       Preserve the existing direct-backend contract without target-specific
       instructions: convert once to unsigned int, subtract '0' modulo the
       unsigned width, then compare against 10.  The unsigned range test is
       true exactly for the ten decimal digit codes, including for negative
       int inputs where the subtraction wraps above the range. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_ISDIGIT) {
        const MinicExpression *operand;
        MinicCoreInstruction builtin_instruction;
        MinicCoreLowerStatus status;
        MinicCoreValueId operand_value;
        MinicCoreValueId normalized_value;
        MinicCoreValueId zero_code;
        MinicCoreValueId offset_value;
        MinicCoreValueId digit_count;

        operand = minic_c0_program_expression(
            context->body->program, expression->value.builtin_unary.operand);
        if (operand == NULL || !minic_type_equal(operand->type, minic_type_int()) ||
            !minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(
            context, expression->value.builtin_unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = append_integer_conversion(context,
                                           operand->span,
                                           minic_type_unsigned_int(),
                                           operand_value,
                                           &normalized_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_unsigned_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.integer_value = 48;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &zero_code)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_unsigned_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.binary.left = normalized_value;
        builtin_instruction.value.binary.right = zero_code;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &offset_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_unsigned_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.integer_value = 10;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &digit_count)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.binary.left = offset_value;
        builtin_instruction.value.binary.right = digit_count;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &builtin_instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

    /* M175F_RUNTIME_FFSLL_OWNER: __builtin_ffsll returns a one-based
       least-significant-set-bit index and zero for a zero operand.  Reuse the
       existing unsigned-long CTZ semantic owner instead of introducing a
       target-specific FFS instruction.  RV64's long and long long are both
       64-bit, so converting signed long long to unsigned long preserves the
       operand bit pattern modulo 2^64.  The double zero-test yields a canonical
       int nonzero mask; multiplying by it maps the CTZ zero sentinel (64) back
       to the required ffsll zero result. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_FFSLL) {
        const MinicExpression *operand;
        MinicCoreInstruction builtin_instruction;
        MinicCoreLowerStatus status;
        MinicCoreValueId operand_value;
        MinicCoreValueId unsigned_value;
        MinicCoreValueId trailing_zero_count;
        MinicCoreValueId one;
        MinicCoreValueId one_based_index;
        MinicCoreValueId is_zero;
        MinicCoreValueId is_nonzero;

        operand = minic_c0_program_expression(
            context->body->program, expression->value.builtin_unary.operand);
        if (operand == NULL || !minic_type_equal(operand->type, minic_type_long_long()) ||
            !minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(
            context, expression->value.builtin_unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type,
                              minic_type_long_long())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = append_integer_conversion(
            context, operand->span, minic_type_unsigned_long(), operand_value, &unsigned_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CTZ;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.operand = unsigned_value;
        if (!minic_core_function_append_value_instruction(
                context->function,
                context->block_id,
                &builtin_instruction,
                &trailing_zero_count)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.integer_value = 1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &one)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.binary.left = trailing_zero_count;
        builtin_instruction.value.binary.right = one;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &one_based_index)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.operand = unsigned_value;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &is_zero)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.operand = is_zero;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &is_nonzero)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.binary.left = one_based_index;
        builtin_instruction.value.binary.right = is_nonzero;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &builtin_instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        (expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZ ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZL ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZLL ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CTZ ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CTZL ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CTZLL)) {
        const MinicExpression *operand;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;
        bool is_clz;

        operand = minic_c0_program_expression(
            context->body->program, expression->value.builtin_unary.operand);
        if (operand == NULL || !minic_type_is_unsigned_integer(operand->type) ||
            !minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(
            context, expression->value.builtin_unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type, operand->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        is_clz = expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZ ||
                 expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZL ||
                 expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZLL;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = is_clz ? MINIC_CORE_INSTRUCTION_INTEGER_CLZ
                                  : MINIC_CORE_INSTRUCTION_INTEGER_CTZ;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

    if (expression->kind == MINIC_EXPRESSION_CALL) {
        if (expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
            return lower_indirect_call(context, expression, value_id);
        }
        return lower_direct_call(context, expression, value_id);
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
        return lower_scalar_update(context, expression, value_id);
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
        status = append_integer_conversion(
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
        status = append_integer_conversion(
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
        if (!minic_type_is_double(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
        instruction.value.floating_bits = expression->value.floating_bits;
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
            return append_integer_conversion(
                context, expression->span, target_type, operand_value, value_id);
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.span = expression->span;
        instruction.type = target_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        if (minic_type_is_double(target_type) && minic_type_is_integer(source_type)) {
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE;
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
            status = append_integer_conversion(
                context, left_expression->span, left_value_type, left_source, &left);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        status =
            spill_scalar_value(context, left_expression->span, left_value_type, left, &left_object);
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
            status = append_integer_conversion(
                context, right_expression->span, right_value_type, right_source, &right);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        status =
            spill_scalar_value(context, right_expression->span, right_value_type, right, &right_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        status =
            lower_expression(context, expression->value.overflow.result_pointer, &result_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            reload_scalar_value(context, left_expression->span, left_value_type, left_object, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            reload_scalar_value(context, right_expression->span, right_value_type, right_object, &right);
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

    /* RUNTIME_R0_DOUBLE_COMPARE: equality uses the same binary64
       assignment-conversion seam as arithmetic, so mixed integer/double
       operands are normalized before Core comparison. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicType left_type;
        MinicType right_type;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !core_scalar_expression_value_type(context->body, left_expression, &left_type) ||
            !core_scalar_expression_value_type(context->body, right_expression, &right_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_double(left_type) || minic_type_is_double(right_type)) {
            MinicCoreInstruction zero_test_instruction;
            MinicCoreValueId equal_value;
            MinicCoreValueId left;
            MinicCoreValueId right;
            MinicCoreLowerStatus status;

            if (!minic_type_equal(expression->type, minic_type_int())) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = lower_double_binary_operands(context,
                                                  expression->value.binary.left,
                                                  expression->value.binary.right,
                                                  minic_type_double(),
                                                  &left,
                                                  &right);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL;
            instruction.span = expression->span;
            instruction.type = minic_type_int();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.binary.left = left;
            instruction.value.binary.right = right;
            if (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL) {
                return minic_core_function_append_value_instruction(
                           context->function, context->block_id, &instruction, value_id)
                           ? MINIC_CORE_LOWER_OK
                           : MINIC_CORE_LOWER_ERROR;
            }
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &equal_value)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&zero_test_instruction, 0, sizeof(zero_test_instruction));
            zero_test_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
            zero_test_instruction.span = expression->span;
            zero_test_instruction.type = minic_type_int();
            zero_test_instruction.result = MINIC_CORE_VALUE_INVALID;
            zero_test_instruction.value.operand = equal_value;
            return minic_core_function_append_value_instruction(
                       context->function,
                       context->block_id,
                       &zero_test_instruction,
                       value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_EQUAL) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_scalar_equality_operands(
            context, expression->value.binary.left, expression->value.binary.right, &left, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) {
        MinicCoreInstruction zero_test_instruction;
        MinicCoreValueId equal_value;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_scalar_equality_operands(
            context, expression->value.binary.left, expression->value.binary.right, &left, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &equal_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&zero_test_instruction, 0, sizeof(zero_test_instruction));
        zero_test_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        zero_test_instruction.span = expression->span;
        zero_test_instruction.type = minic_type_int();
        zero_test_instruction.result = MINIC_CORE_VALUE_INVALID;
        zero_test_instruction.value.operand = equal_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &zero_test_instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    /* Ordered binary64 relational comparisons must remain direct IEEE-754
       predicates.  <= uses LE; > and >= swap operands.  No inversion is used,
       so unordered NaN inputs remain false as required by C. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicType left_type;
        MinicType right_type;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !core_scalar_expression_value_type(context->body, left_expression, &left_type) ||
            !core_scalar_expression_value_type(context->body, right_expression, &right_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_double(left_type) || minic_type_is_double(right_type)) {
            MinicCoreValueId left;
            MinicCoreValueId right;
            MinicCoreLowerStatus status;
            bool swap;

            if (!minic_type_equal(expression->type, minic_type_int())) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = lower_double_binary_operands(context,
                                                  expression->value.binary.left,
                                                  expression->value.binary.right,
                                                  minic_type_double(),
                                                  &left,
                                                  &right);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            swap = expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
                   expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL;
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind =
                expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
                        expression->value.binary.operator_kind == MINIC_BINARY_GREATER
                    ? MINIC_CORE_INSTRUCTION_DOUBLE_LESS
                    : MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL;
            instruction.span = expression->span;
            instruction.type = minic_type_int();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.binary.left = swap ? right : left;
            instruction.value.binary.right = swap ? left : right;
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreInstruction invert_instruction;
        MinicCoreValueId left;
        MinicCoreValueId less_value;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;
        MinicType common_type;
        MinicType left_type;
        MinicType right_type;
        bool invert;
        bool swap;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression != NULL && right_expression != NULL &&
            core_scalar_expression_value_type(context->body, left_expression, &left_type) &&
            core_scalar_expression_value_type(context->body, right_expression, &right_type) &&
            minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type)) {
            /* M156_STRUCTURAL_POINTER_RELATIONAL_OWNER: relational
               legality is already decided by frontend/Sema, including GNU void
               pointers and structurally compatible pointer-to-array shapes.
               Core only needs one bit representation for POINTER_LESS.  The
               ordinary conditional-pointer common type remains preferred; if
               side-table identity prevents one despite accepted relational
               compatibility, use the left representation and bitcast both
               operands, matching the established equality owner. */
            if (!minic_c0_pointer_relational_compatible(
                    context->body->program, left_type, right_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            if (!minic_type_conditional_pointer_common(
                    left_type, right_type, &common_type)) {
                common_type = left_type;
            }
            status = lower_expression(context, expression->value.binary.left, &left);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (left >= context->function->value_count) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!minic_type_equal(context->function->values[left].type, common_type)) {
                status = append_scalar_bitcast(
                    context, left_expression->span, common_type, left, &left);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            /* M117_BLOCK_LOCAL_POINTER_RELATIONAL: lowering the right operand
               may create a new Core block. Spill the normalized left pointer so
               the eventual POINTER_LESS never references an SSA value owned by
               a predecessor block. */
            MinicCoreObjectId left_object;
            status = spill_scalar_value(
                context, left_expression->span, common_type, left, &left_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = lower_expression(context, expression->value.binary.right, &right);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (right >= context->function->value_count) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!minic_type_equal(context->function->values[right].type, common_type)) {
                status = append_scalar_bitcast(
                    context, right_expression->span, common_type, right, &right);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            /* M117_BLOCK_LOCAL_POINTER_RELATIONAL: reload only after the right
               operand is fully lowered and normalized, in the final comparison
               block. */
            status = reload_scalar_value(
                context, left_expression->span, common_type, left_object, &left);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            swap = expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
                   expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL;
            invert = expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
                     expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL;
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_LESS;
            instruction.span = expression->span;
            instruction.type = minic_type_int();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.binary.left = swap ? right : left;
            instruction.value.binary.right = swap ? left : right;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &less_value)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!invert) {
                *value_id = less_value;
                return MINIC_CORE_LOWER_OK;
            }
            (void)memset(&invert_instruction, 0, sizeof(invert_instruction));
            invert_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
            invert_instruction.span = expression->span;
            invert_instruction.type = minic_type_int();
            invert_instruction.result = MINIC_CORE_VALUE_INVALID;
            invert_instruction.value.operand = less_value;
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &invert_instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LESS) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;
        MinicType common_type;

        if (!minic_type_equal(expression->type, minic_type_int()) || context->target == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !minic_type_is_integer(left_expression->type) ||
            !minic_type_is_integer(right_expression->type) ||
            !minic_target_info_integer_common_for_program(context->target,
                                                          context->body->program,
                                                          left_expression->type,
                                                          right_expression->type,
                                                          &common_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               common_type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreInstruction invert_instruction;
        MinicCoreValueId left;
        MinicCoreValueId less_value;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;
        MinicType common_type;
        MinicType left_type;
        MinicType right_type;
        bool invert;
        bool swap;

        if (!minic_type_equal(expression->type, minic_type_int()) || context->target == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !core_scalar_expression_value_type(context->body, left_expression, &left_type) ||
            !core_scalar_expression_value_type(context->body, right_expression, &right_type) ||
            !minic_type_is_integer(left_type) || !minic_type_is_integer(right_type) ||
            !minic_target_info_integer_common_for_program(
                context->target, context->body->program, left_type, right_type, &common_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               common_type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        swap = expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
               expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL;
        invert = expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
                 expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = swap ? right : left;
        instruction.value.binary.right = swap ? left : right;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &less_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!invert) {
            *value_id = less_value;
            return MINIC_CORE_LOWER_OK;
        }
        (void)memset(&invert_instruction, 0, sizeof(invert_instruction));
        invert_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        invert_instruction.span = expression->span;
        invert_instruction.type = minic_type_int();
        invert_instruction.result = MINIC_CORE_VALUE_INVALID;
        invert_instruction.value.operand = less_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &invert_instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    /* M93_POINTER_DIFFERENCE: pointer - pointer produces an integer
       element distance. Compose existing target-neutral Core scalar primitives:
       pointer-to-integer bitcasts, byte subtraction, then signed division by
       the semantic pointee stride. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT &&
        minic_type_is_integer(expression->type)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreLowerStatus status;
        MinicCoreObjectId left_object;
        MinicCoreValueId left_pointer;
        MinicCoreValueId right_pointer;
        MinicCoreValueId left_integer;
        MinicCoreValueId right_integer;
        MinicCoreValueId byte_difference;
        MinicCoreValueId divisor;
        MinicType left_type;
        MinicType right_type;
        size_t element_size;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression != NULL && right_expression != NULL &&
            core_scalar_expression_value_type(context->body, left_expression, &left_type) &&
            core_scalar_expression_value_type(context->body, right_expression, &right_type) &&
            minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type) &&
            /* BATCH_W_QUALIFIED_POINTER_DIFFERENCE: language compatibility is
               owned by frontend/Sema.  `const T * - T *` is a valid pointer
               difference when the pointed-to object types are compatible;
               Core must not re-impose exact pointer-type equality after Sema
               has accepted the expression.  The representation and stride
               lowering below remain target-neutral and unchanged. */
            minic_c0_pointer_difference_compatible(
                context->body->program, left_type, right_type) &&
            minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      core_data_layout(context),
                                                      left_type,
                                                      &element_size) &&
            /* M131_ZERO_STRIDE_POINTER_DIFFERENCE_FAIL_CLOSED: unlike pointer
               +/- integer, pointer difference divides by the pointee stride. */
            element_size != 0U) {
            status = lower_expression(
                context, expression->value.binary.left, &left_pointer);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = append_scalar_bitcast(
                context, left_expression->span, expression->type, left_pointer, &left_integer);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = spill_scalar_value(
                context, left_expression->span, expression->type, left_integer, &left_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = lower_expression(
                context, expression->value.binary.right, &right_pointer);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = append_scalar_bitcast(
                context, right_expression->span, expression->type, right_pointer, &right_integer);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = reload_scalar_value(
                context, left_expression->span, expression->type, left_object, &left_integer);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }

            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
            instruction.span = expression->span;
            instruction.type = expression->type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.binary.left = left_integer;
            instruction.value.binary.right = right_integer;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &byte_difference)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (element_size == 1U) {
                *value_id = byte_difference;
                return MINIC_CORE_LOWER_OK;
            }

            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            instruction.span = expression->span;
            instruction.type = expression->type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.integer_value = (int64_t)element_size;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &divisor)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE;
            instruction.span = expression->span;
            instruction.type = expression->type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.binary.left = byte_difference;
            instruction.value.binary.right = divisor;
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    /* M82_BINARY_POINTER_SUBTRACTION: C/GNU pointer +/- integer share the
       same scaled-offset primitive. Subtraction is only valid with the pointer
       on the left; integer - pointer remains fail-closed. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT) &&
        minic_type_is_pointer(expression->type)) {
        const MinicExpression *left_expression;
        const MinicExpression *pointer_expression;
        const MinicExpression *right_expression;
        const MinicExpression *index_expression;
        MinicExpressionId pointer_id;
        MinicExpressionId index_id;
        MinicCoreObjectId pointer_object;
        MinicCoreValueId pointer_value;
        MinicCoreValueId index_value;
        MinicCoreValueId offset_value;
        MinicCoreLowerStatus status;
        MinicType expression_value_type;
        MinicType pointer_source_type;
        MinicType pointer_value_type;
        MinicType index_value_type;
        size_t element_size;

        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_pointer(left_expression->type) &&
            minic_type_is_integer(right_expression->type)) {
            pointer_expression = left_expression;
            index_expression = right_expression;
            pointer_id = expression->value.binary.left;
            index_id = expression->value.binary.right;
        } else if (expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
                   minic_type_is_integer(left_expression->type) &&
                   minic_type_is_pointer(right_expression->type)) {
            pointer_expression = right_expression;
            index_expression = left_expression;
            pointer_id = expression->value.binary.right;
            index_id = expression->value.binary.left;
        } else {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        /* BATCH_U_POINTER_ARITH_VALUE_TYPES: Core consumes scalar values, not
           lvalue storage qualifiers.  A member reached through `const T *`
           has a const-qualified lvalue type in the semantic AST, but its
           lvalue-to-rvalue result is the unqualified scalar value transported
           by Core.  Use the shared value-type seam for both operands instead
           of comparing emitted values against raw expression storage types. */
        /* M116_POINTER_ARITH_RVALUE_TYPE: pointer arithmetic consumes and
           produces C scalar values.  A nested arithmetic expression may retain
           a top-level qualifier in the semantic AST spelling, but that qualifier
           belongs to the source object, not the transported rvalue.  Canonicalize
           only the pointer operand's top-level qualifier here; pointee qualifiers
           remain part of the pointer type. */
        if (!core_scalar_expression_value_type(
                context->body, pointer_expression, &pointer_source_type) ||
            !minic_type_unqualified(pointer_source_type, &pointer_value_type) ||
            !core_scalar_expression_value_type(
                context->body, index_expression, &index_value_type) ||
            !minic_type_is_pointer(pointer_value_type) ||
            !minic_type_is_integer(index_value_type) ||
            !minic_type_unqualified(expression->type, &expression_value_type) ||
            !minic_type_equal(pointer_value_type, expression_value_type) ||
            !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      core_data_layout(context),
                                                      pointer_value_type,
                                                      &element_size)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, pointer_id, &pointer_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = spill_scalar_value(context,
                                    pointer_expression->span,
                                    pointer_value_type,
                                    pointer_value,
                                    &pointer_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, index_id, &index_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(context,
                                     pointer_expression->span,
                                     pointer_value_type,
                                     pointer_object,
                                     &pointer_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (pointer_value >= context->function->value_count ||
            index_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[pointer_value].type,
                              pointer_value_type) ||
            !minic_type_equal(context->function->values[index_value].type,
                              index_value_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        instruction.type = pointer_value_type;
        instruction.value.pointer_offset.base = pointer_value;
        instruction.value.pointer_offset.index = index_value;
        instruction.value.pointer_offset.element_size = element_size;
        /* M116_POINTER_ARITH_RVALUE_TYPE: POINTER_OFFSET already has the
           canonical lvalue-to-rvalue pointer type.  Do not re-attach a top-level
           qualifier merely because the AST keeps that source spelling; nested
           pointer arithmetic and return/assignment conversion consume the value
           type, not the storage-qualified spelling. */
        instruction.value.pointer_offset.subtract =
            expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &offset_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = offset_value;
        return MINIC_CORE_LOWER_OK;
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
        status = lower_integer_binary_operands(context,
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
    /* RUNTIME_R0_SCALAR_DOUBLE: preserve left-to-right evaluation by spilling
       the left binary64 value across RHS lowering. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE) &&
        minic_type_is_double(expression->type)) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        status = lower_double_binary_operands(context,
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
            instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_ADD;
            break;
        case MINIC_BINARY_SUBTRACT:
            instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT;
            break;
        case MINIC_BINARY_MULTIPLY:
            instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY;
            break;
        case MINIC_BINARY_DIVIDE:
            instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE;
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
        status = lower_integer_binary_operands(context,
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
        status = lower_integer_binary_operands(context,
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
        status = lower_integer_binary_operands(context,
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
        status = append_integer_conversion(
            context, left_expression->span, expression->type, left_source, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = spill_scalar_value(
            context, left_expression->span, expression->type, left, &left_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(
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
    /* M101_UNSIGNED_BIT_FIELD_COMPOUND_ASSIGNMENT: a bit-field is not a
       C-addressable lvalue, but compound assignment still evaluates its base
       once, reads the field value, performs the promoted operation, then
       writes the converted result back through one storage-unit RMW. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
        const MinicExpression *bit_target;
        const MinicExpression *bit_source;
        const MinicExpression *bit_base;
        const MinicRecord *bit_record;
        const MinicRecordField *bit_field;

        bit_target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        bit_source = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        bit_base = bit_target != NULL && bit_target->kind == MINIC_EXPRESSION_MEMBER
                       ? minic_c0_program_expression(
                             context->body->program, bit_target->value.member.base)
                       : NULL;
        bit_record = bit_target != NULL && bit_target->kind == MINIC_EXPRESSION_MEMBER
                         ? minic_c0_program_record(
                               context->body->program, bit_target->value.member.record_id)
                         : NULL;
        bit_field = bit_target != NULL && bit_target->kind == MINIC_EXPRESSION_MEMBER
                        ? minic_c0_record_field(
                              bit_record, bit_target->value.member.field_index)
                        : NULL;
        if (bit_field != NULL && bit_field->is_bit_field) {
            MinicCoreInstruction bit_instruction;
            MinicCoreObjectId bit_address_object;
            MinicCoreObjectId bit_current_object;
            MinicCoreValueId bit_address;
            MinicCoreValueId bit_base_value;
            MinicCoreValueId bit_constant;
            MinicCoreValueId bit_current;
            MinicCoreValueId bit_current_common;
            MinicCoreValueId bit_field_storage;
            MinicCoreValueId bit_merge_current;
            MinicCoreValueId bit_merged;
            MinicCoreValueId bit_result;
            MinicCoreValueId bit_rhs;
            MinicCoreValueId bit_rhs_common;
            MinicCoreValueId bit_stored_value;
            MinicCoreLowerStatus bit_status;
            MinicType bit_address_type;
            MinicType bit_base_value_type;
            MinicType bit_common_type;
            MinicType bit_expression_value_type;
            MinicType bit_record_type;
            MinicType bit_right_type;
            MinicType bit_storage_access_type;
            MinicType bit_storage_type;
            MinicType bit_value_type;
            size_t bit_byte_offset;
            size_t bit_offset;
            unsigned int bit_storage_width;
            uint64_t bit_clear_mask;
            uint64_t bit_field_mask;
            uint64_t bit_low_mask;
            uint64_t bit_storage_mask;
            bool bit_shift_assignment;

            if (bit_target == NULL || bit_source == NULL || bit_base == NULL ||
                bit_record == NULL || bit_target->value_category != MINIC_VALUE_LVALUE ||
                bit_field->bit_width == 0U || minic_type_is_const(bit_target->type) ||
                !minic_type_unqualified(bit_target->type, &bit_value_type) ||
                !minic_type_is_integer(bit_value_type) ||
                (!core_unsigned_bit_field_semantic_type(context, bit_value_type) &&
                 !minic_type_is_signed_integer(bit_value_type)) ||
                !minic_type_is_integer(bit_source->type) || context->target == NULL ||
                !minic_type_unqualified(expression->type, &bit_expression_value_type) ||
                !minic_type_equal(bit_expression_value_type, bit_value_type) ||
                !core_bit_field_storage_type(
                    context, bit_value_type, &bit_storage_type, &bit_storage_width) ||
                bit_storage_width == 0U || bit_storage_width > 64U ||
                bit_field->bit_width > bit_storage_width ||
                !minic_data_layout_record_field_layout(core_data_layout(context),
                                                       context->body->program,
                                                       bit_record,
                                                       bit_target->value.member.field_index,
                                                       &bit_byte_offset,
                                                       &bit_offset) ||
                bit_offset + bit_field->bit_width > bit_storage_width ||
                !core_scalar_expression_value_type(
                    context->body, bit_base, &bit_base_value_type) ||
                !minic_type_is_pointer(bit_base_value_type) ||
                !minic_type_pointee(bit_base_value_type, &bit_record_type) ||
                !minic_type_is_record(bit_record_type) ||
                bit_record_type.record_id != bit_target->value.member.record_id) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            (void)bit_byte_offset;
            bit_shift_assignment =
                expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
                expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT;
            if (bit_shift_assignment) {
                if (!minic_target_info_integer_promotion_for_program(
                        context->target,
                        context->body->program,
                        bit_value_type,
                        &bit_common_type) ||
                    !minic_target_info_integer_promotion_for_program(
                        context->target,
                        context->body->program,
                        bit_source->type,
                        &bit_right_type)) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
            } else {
                if (!minic_target_info_integer_common_for_program(context->target,
                                                                  context->body->program,
                                                                  bit_value_type,
                                                                  bit_source->type,
                                                                  &bit_common_type)) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                bit_right_type = bit_common_type;
            }
            bit_storage_access_type = bit_storage_type;
            if (minic_type_is_volatile(bit_target->type) &&
                !minic_type_add_volatile(bit_storage_access_type, &bit_storage_access_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            bit_status = lower_expression(
                context, bit_target->value.member.base, &bit_base_value);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            if (bit_base_value >= context->function->value_count ||
                !minic_type_equal(context->function->values[bit_base_value].type,
                                  bit_base_value_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            bit_status = append_field_address(context,
                                              bit_target->span,
                                              bit_base_value,
                                              bit_target->value.member.record_id,
                                              bit_target->value.member.field_index,
                                              bit_storage_access_type,
                                              &bit_address);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
            bit_instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
            bit_instruction.span = bit_target->span;
            bit_instruction.type = bit_storage_type;
            bit_instruction.result = MINIC_CORE_VALUE_INVALID;
            bit_instruction.value.load.address = bit_address;
            bit_instruction.value.load.is_volatile = minic_type_is_volatile(bit_target->type);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit_instruction, &bit_current)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (bit_offset != 0U) {
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = minic_type_unsigned_int();
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.integer_value = (int64_t)bit_offset;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &bit_instruction, &bit_constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.binary.left = bit_current;
                bit_instruction.value.binary.right = bit_constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &bit_instruction, &bit_current)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            bit_low_mask = bit_field->bit_width == 64U
                               ? UINT64_MAX
                               : ((UINT64_C(1) << bit_field->bit_width) - UINT64_C(1));
            if (bit_field->bit_width < bit_storage_width) {
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&bit_instruction.value.integer_value,
                             &bit_low_mask,
                             sizeof(bit_low_mask));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &bit_instruction, &bit_constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.binary.left = bit_current;
                bit_instruction.value.binary.right = bit_constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &bit_instruction, &bit_current)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (!minic_type_equal(bit_storage_type, bit_value_type)) {
                bit_status = append_integer_conversion(context,
                                                       bit_target->span,
                                                       bit_value_type,
                                                       bit_current,
                                                       &bit_current);
                if (bit_status != MINIC_CORE_LOWER_OK) {
                    return bit_status;
                }
            }
            if (minic_type_is_signed_integer(bit_value_type) &&
                bit_field->bit_width < bit_storage_width) {
                MinicCoreValueId bit_sign_shift;
                uint64_t bit_sign_shift_bits =
                    (uint64_t)(bit_storage_width - bit_field->bit_width);

                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = minic_type_unsigned_int();
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&bit_instruction.value.integer_value,
                             &bit_sign_shift_bits,
                             sizeof(bit_sign_shift_bits));
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_sign_shift)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = bit_value_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.binary.left = bit_current;
                bit_instruction.value.binary.right = bit_sign_shift;
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_current)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = bit_value_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.binary.left = bit_current;
                bit_instruction.value.binary.right = bit_sign_shift;
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_current)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            bit_status = append_integer_conversion(context,
                                                   bit_target->span,
                                                   bit_common_type,
                                                   bit_current,
                                                   &bit_current_common);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_address_type = context->function->values[bit_address].type;
            bit_status = spill_scalar_value(
                context, bit_target->span, bit_address_type, bit_address, &bit_address_object);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = spill_scalar_value(context,
                                            bit_target->span,
                                            bit_common_type,
                                            bit_current_common,
                                            &bit_current_object);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = lower_expression(
                context, expression->value.binary.right, &bit_rhs);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = append_integer_conversion(
                context, bit_source->span, bit_right_type, bit_rhs, &bit_rhs_common);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = reload_scalar_value(context,
                                             bit_target->span,
                                             bit_common_type,
                                             bit_current_object,
                                             &bit_current_common);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = reload_scalar_value(context,
                                             bit_target->span,
                                             bit_address_type,
                                             bit_address_object,
                                             &bit_address);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
            switch (expression->value.binary.operator_kind) {
            case MINIC_BINARY_ADD:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
                break;
            case MINIC_BINARY_SUBTRACT:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
                break;
            case MINIC_BINARY_MULTIPLY:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY;
                break;
            case MINIC_BINARY_DIVIDE:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE;
                break;
            case MINIC_BINARY_REMAINDER:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER;
                break;
            case MINIC_BINARY_SHIFT_LEFT:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                break;
            case MINIC_BINARY_SHIFT_RIGHT:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                break;
            case MINIC_BINARY_BITWISE_AND:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
                break;
            case MINIC_BINARY_BITWISE_XOR:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR;
                break;
            case MINIC_BINARY_BITWISE_OR:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
                break;
            default:
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            bit_instruction.span = expression->span;
            bit_instruction.type = bit_common_type;
            bit_instruction.result = MINIC_CORE_VALUE_INVALID;
            bit_instruction.value.binary.left = bit_current_common;
            bit_instruction.value.binary.right = bit_rhs_common;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit_instruction, &bit_result)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            bit_status = append_integer_conversion(context,
                                                   expression->span,
                                                   bit_value_type,
                                                   bit_result,
                                                   &bit_stored_value);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            if (minic_type_equal(bit_storage_type, bit_value_type)) {
                bit_field_storage = bit_stored_value;
            } else {
                bit_status = append_integer_conversion(context,
                                                       expression->span,
                                                       bit_storage_type,
                                                       bit_stored_value,
                                                       &bit_field_storage);
                if (bit_status != MINIC_CORE_LOWER_OK) {
                    return bit_status;
                }
            }
            if (bit_field->bit_width < bit_storage_width) {
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                bit_instruction.span = expression->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&bit_instruction.value.integer_value,
                             &bit_low_mask,
                             sizeof(bit_low_mask));
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
                bit_instruction.span = expression->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.binary.left = bit_field_storage;
                bit_instruction.value.binary.right = bit_constant;
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_field_storage)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (minic_type_is_signed_integer(bit_value_type)) {
                if (!minic_type_equal(bit_storage_type, bit_value_type)) {
                    bit_status = append_integer_conversion(context,
                                                           expression->span,
                                                           bit_value_type,
                                                           bit_field_storage,
                                                           &bit_stored_value);
                    if (bit_status != MINIC_CORE_LOWER_OK) {
                        return bit_status;
                    }
                } else {
                    bit_stored_value = bit_field_storage;
                }
                if (bit_field->bit_width < bit_storage_width) {
                    MinicCoreValueId bit_sign_shift;
                    uint64_t bit_sign_shift_bits =
                        (uint64_t)(bit_storage_width - bit_field->bit_width);

                    (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                    bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                    bit_instruction.span = expression->span;
                    bit_instruction.type = minic_type_unsigned_int();
                    bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                    (void)memcpy(&bit_instruction.value.integer_value,
                                 &bit_sign_shift_bits,
                                 sizeof(bit_sign_shift_bits));
                    if (!minic_core_function_append_value_instruction(
                            context->function,
                            context->block_id,
                            &bit_instruction,
                            &bit_sign_shift)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                    (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                    bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                    bit_instruction.span = expression->span;
                    bit_instruction.type = bit_value_type;
                    bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                    bit_instruction.value.binary.left = bit_stored_value;
                    bit_instruction.value.binary.right = bit_sign_shift;
                    if (!minic_core_function_append_value_instruction(
                            context->function,
                            context->block_id,
                            &bit_instruction,
                            &bit_stored_value)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                    (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                    bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                    bit_instruction.span = expression->span;
                    bit_instruction.type = bit_value_type;
                    bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                    bit_instruction.value.binary.left = bit_stored_value;
                    bit_instruction.value.binary.right = bit_sign_shift;
                    if (!minic_core_function_append_value_instruction(
                            context->function,
                            context->block_id,
                            &bit_instruction,
                            &bit_stored_value)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                }
            }
            if (bit_offset != 0U) {
                uint64_t bit_shift = (uint64_t)bit_offset;
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                bit_instruction.span = expression->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&bit_instruction.value.integer_value,
                             &bit_shift,
                             sizeof(bit_shift));
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                bit_instruction.span = expression->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.binary.left = bit_field_storage;
                bit_instruction.value.binary.right = bit_constant;
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_field_storage)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            bit_field_mask = bit_low_mask << bit_offset;
            bit_storage_mask = bit_storage_width == 64U
                                   ? UINT64_MAX
                                   : ((UINT64_C(1) << bit_storage_width) - UINT64_C(1));
            bit_clear_mask = (~bit_field_mask) & bit_storage_mask;
            (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
            bit_instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
            bit_instruction.span = expression->span;
            bit_instruction.type = bit_storage_type;
            bit_instruction.result = MINIC_CORE_VALUE_INVALID;
            bit_instruction.value.load.address = bit_address;
            bit_instruction.value.load.is_volatile = minic_type_is_volatile(bit_target->type);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit_instruction, &bit_merge_current)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
            bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            bit_instruction.span = expression->span;
            bit_instruction.type = bit_storage_type;
            bit_instruction.result = MINIC_CORE_VALUE_INVALID;
            (void)memcpy(&bit_instruction.value.integer_value,
                         &bit_clear_mask,
                         sizeof(bit_clear_mask));
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit_instruction, &bit_constant)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
            bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
            bit_instruction.span = expression->span;
            bit_instruction.type = bit_storage_type;
            bit_instruction.result = MINIC_CORE_VALUE_INVALID;
            bit_instruction.value.binary.left = bit_merge_current;
            bit_instruction.value.binary.right = bit_constant;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit_instruction, &bit_merged)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
            bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
            bit_instruction.span = expression->span;
            bit_instruction.type = bit_storage_type;
            bit_instruction.result = MINIC_CORE_VALUE_INVALID;
            bit_instruction.value.binary.left = bit_merged;
            bit_instruction.value.binary.right = bit_field_storage;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit_instruction, &bit_merged)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
            bit_instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
            bit_instruction.span = expression->span;
            bit_instruction.type = minic_type_void();
            bit_instruction.result = MINIC_CORE_VALUE_INVALID;
            bit_instruction.value.store.address = bit_address;
            bit_instruction.value.store.stored_value = bit_merged;
            bit_instruction.value.store.is_volatile = minic_type_is_volatile(bit_target->type);
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &bit_instruction)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            *value_id = bit_stored_value;
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
            append_integer_conversion(context, target->span, common_type, current, &current_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (address >= context->function->value_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        address_type = context->function->values[address].type;
        status = spill_scalar_value(context, target->span, address_type, address, &address_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            spill_scalar_value(context, target->span, common_type, current_common, &current_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            append_integer_conversion(context, source->span, right_type, right, &right_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(
            context, target->span, common_type, current_object, &current_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(context, target->span, address_type, address_object, &address);
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
        status = append_integer_conversion(
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

static MinicCoreLowerStatus lower_assignment_pair(MinicCoreLowerContext *context,
                                                  MinicExpressionId target_id,
                                                  MinicExpressionId source_id,
                                                  MinicSourceSpan span,
                                                  MinicCoreValueId *result_value) {
    const MinicExpression *target;
    const MinicExpression *source;
    const MinicExpression *source_operand;
    MinicCoreInstruction instruction;
    MinicCoreObjectId stored_object;
    MinicCoreValueId address_id;
    MinicCoreValueId stored_value;
    MinicCoreLowerStatus status;
    MinicType stored_type;
    int source_kind;
    int source_operand_kind;

    if (context == NULL || context->body == NULL || context->body->program == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    target = minic_c0_program_expression(context->body->program, target_id);
    source = minic_c0_program_expression(context->body->program, source_id);
    source_operand = NULL;
    source_kind = source != NULL ? (int)source->kind : -1;
    source_operand_kind = -1;
    if (source != NULL && source->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        source_operand = minic_c0_program_expression(
            context->body->program, source->value.unary.operand);
        if (source_operand != NULL) {
            source_operand_kind = (int)source_operand->kind;
        }
    }
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE) {
        return MINIC_CORE_LOWER_ERROR;
    }
    /* BATCH_P_UNSIGNED_BIT_FIELD_WRITE: bit-fields are not C-addressable, so
       lower a simple unsigned bit-field assignment as one storage-unit RMW.
       Reuse the same field-layout/address seam as the established unsigned
       bit-field read. Signed bit-field writes remain fail-closed. */
    if (target->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;

        base = minic_c0_program_expression(context->body->program, target->value.member.base);
        record = minic_c0_program_record(context->body->program, target->value.member.record_id);
        field = minic_c0_record_field(record, target->value.member.field_index);
        if (field != NULL && field->is_bit_field) {
            MinicCoreInstruction operation;
            MinicCoreObjectId source_object;
            MinicCoreValueId address;
            MinicCoreValueId base_value;
            MinicCoreValueId current;
            MinicCoreValueId field_value;
            MinicCoreValueId field_storage;
            MinicCoreValueId assigned_value;
            MinicCoreValueId constant;
            MinicCoreValueId merged;
            MinicCoreLowerStatus bit_status;
            MinicType base_value_type;
            MinicType record_type;
            MinicType storage_access_type;
            MinicType storage_type;
            MinicType value_type;
            size_t byte_offset;
            size_t bit_offset;
            unsigned int storage_width;
            uint64_t low_mask;
            uint64_t field_mask;
            uint64_t clear_mask;
            uint64_t storage_mask;

            if (base == NULL || record == NULL || field->bit_width == 0U ||
                !minic_type_unqualified(target->type, &value_type) ||
                !minic_type_is_integer(value_type) ||
                (!core_unsigned_bit_field_semantic_type(context, value_type) &&
                 !minic_type_is_signed_integer(value_type)) ||
                minic_type_is_const(target->type) ||
                !core_bit_field_storage_type(
                    context, value_type, &storage_type, &storage_width) ||
                storage_width == 0U || storage_width > 64U ||
                field->bit_width > storage_width ||
                !minic_data_layout_record_field_layout(core_data_layout(context),
                                                       context->body->program,
                                                       record,
                                                       target->value.member.field_index,
                                                       &byte_offset,
                                                       &bit_offset) ||
                bit_offset + field->bit_width > storage_width ||
                !core_scalar_expression_value_type(context->body, base, &base_value_type) ||
                !minic_type_is_pointer(base_value_type) ||
                !minic_type_pointee(base_value_type, &record_type) ||
                !minic_type_is_record(record_type) ||
                record_type.record_id != target->value.member.record_id) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            (void)byte_offset;
            storage_access_type = storage_type;
            if (minic_type_is_volatile(target->type) &&
                !minic_type_add_volatile(storage_access_type, &storage_access_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }

            bit_status = lower_scalar_assignment_value(
                context, value_type, source_id, &field_value);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = spill_scalar_value(
                context, span, value_type, field_value, &source_object);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }

            bit_status = lower_expression(context, target->value.member.base, &base_value);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            if (base_value >= context->function->value_count ||
                !minic_type_equal(context->function->values[base_value].type, base_value_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            bit_status = append_field_address(context,
                                              target->span,
                                              base_value,
                                              target->value.member.record_id,
                                              target->value.member.field_index,
                                              storage_access_type,
                                              &address);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            (void)memset(&operation, 0, sizeof(operation));
            operation.kind = MINIC_CORE_INSTRUCTION_LOAD;
            operation.span = target->span;
            operation.type = storage_type;
            operation.result = MINIC_CORE_VALUE_INVALID;
            operation.value.load.address = address;
            operation.value.load.is_volatile = minic_type_is_volatile(target->type);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &operation, &current)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            bit_status = reload_scalar_value(
                context, span, value_type, source_object, &field_value);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            if (minic_type_equal(storage_type, value_type)) {
                field_storage = field_value;
            } else {
                bit_status = append_integer_conversion(
                    context, span, storage_type, field_value, &field_storage);
                if (bit_status != MINIC_CORE_LOWER_OK) {
                    return bit_status;
                }
            }

            low_mask = field->bit_width == 64U
                           ? UINT64_MAX
                           : ((UINT64_C(1) << field->bit_width) - UINT64_C(1));
            if (field->bit_width < storage_width) {
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                operation.span = span;
                operation.type = storage_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&operation.value.integer_value, &low_mask, sizeof(low_mask));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
                operation.span = span;
                operation.type = storage_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                operation.value.binary.left = field_storage;
                operation.value.binary.right = constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &field_storage)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (minic_type_equal(storage_type, value_type)) {
                assigned_value = field_storage;
            } else {
                bit_status = append_integer_conversion(
                    context, span, value_type, field_storage, &assigned_value);
                if (bit_status != MINIC_CORE_LOWER_OK) {
                    return bit_status;
                }
            }
            /* M153_SIGNED_BIT_FIELD_WRITE_OWNER: storage is merged through an
               unsigned allocation unit.  For a signed field, reconstruct the
               assignment-expression value from the truncated field bits using
               the same shift-left/arithmetic-shift-right sign extension as the
               established M103 read path.  The stored bits themselves remain
               the masked two's-complement representation. */
            if (minic_type_is_signed_integer(value_type) &&
                field->bit_width < storage_width) {
                MinicCoreValueId shift;
                uint64_t shift_bits = (uint64_t)(storage_width - field->bit_width);

                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                operation.span = span;
                operation.type = minic_type_unsigned_int();
                operation.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&operation.value.integer_value, &shift_bits, sizeof(shift_bits));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &shift)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                operation.span = span;
                operation.type = value_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                operation.value.binary.left = assigned_value;
                operation.value.binary.right = shift;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &assigned_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                operation.span = span;
                operation.type = value_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                operation.value.binary.left = assigned_value;
                operation.value.binary.right = shift;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &assigned_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (bit_offset != 0U) {
                uint64_t shift_bits = (uint64_t)bit_offset;

                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                operation.span = span;
                operation.type = storage_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&operation.value.integer_value, &shift_bits, sizeof(shift_bits));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                operation.span = span;
                operation.type = storage_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                operation.value.binary.left = field_storage;
                operation.value.binary.right = constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &field_storage)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }

            field_mask = low_mask << bit_offset;
            storage_mask = storage_width == 64U
                               ? UINT64_MAX
                               : ((UINT64_C(1) << storage_width) - UINT64_C(1));
            clear_mask = (~field_mask) & storage_mask;
            (void)memset(&operation, 0, sizeof(operation));
            operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            operation.span = span;
            operation.type = storage_type;
            operation.result = MINIC_CORE_VALUE_INVALID;
            (void)memcpy(&operation.value.integer_value, &clear_mask, sizeof(clear_mask));
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &operation, &constant)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&operation, 0, sizeof(operation));
            operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
            operation.span = span;
            operation.type = storage_type;
            operation.result = MINIC_CORE_VALUE_INVALID;
            operation.value.binary.left = current;
            operation.value.binary.right = constant;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &operation, &merged)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&operation, 0, sizeof(operation));
            operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
            operation.span = span;
            operation.type = storage_type;
            operation.result = MINIC_CORE_VALUE_INVALID;
            operation.value.binary.left = merged;
            operation.value.binary.right = field_storage;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &operation, &merged)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&operation, 0, sizeof(operation));
            operation.kind = MINIC_CORE_INSTRUCTION_STORE;
            operation.span = span;
            operation.type = minic_type_void();
            operation.result = MINIC_CORE_VALUE_INVALID;
            operation.value.store.address = address;
            operation.value.store.stored_value = merged;
            operation.value.store.is_volatile = minic_type_is_volatile(target->type);
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &operation)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (result_value != NULL) {
                *result_value = assigned_value;
            }
            return MINIC_CORE_LOWER_OK;
        }
    }
    if (!minic_type_unqualified(target->type, &stored_type) ||
        !core_memory_scalar_type(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);
    if (status != MINIC_CORE_LOWER_OK) {
        {
            const MinicExpression *target_expression;
            const MinicExpression *source_expression;

            target_expression =
                minic_c0_program_expression(context->body->program, target_id);
            source_expression =
                minic_c0_program_expression(context->body->program, source_id);
            (void)fprintf(stderr,
                          "CORE_ASSIGN_STAGE function=%s stage=value status=%d "
                          "source_kind=%d operand_kind=%d target_base=%d target_ptr=%u "
                          "source_base=%d source_ptr=%u\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          (int)status,
                          source_kind,
                          source_operand_kind,
                          target_expression != NULL ? (int)target_expression->type.base_kind : -1,
                          target_expression != NULL ? target_expression->type.pointer_depth : 0U,
                          source_expression != NULL ? (int)source_expression->type.base_kind : -1,
                          source_expression != NULL ? source_expression->type.pointer_depth : 0U);
        }
        return status;
    }
    status = spill_scalar_value(context, span, stored_type, stored_value, &stored_object);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=spill status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status);
        return status;
    }
    status = lower_address(context, target_id, &address_id);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=target-address status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status);
        return status;
    }
    status = reload_scalar_value(context, span, stored_type, stored_object, &stored_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=reload status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status);
        return status;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address_id;
    instruction.value.store.stored_value = stored_value;
    instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &instruction)) {
        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=store status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)MINIC_CORE_LOWER_ERROR);
        return MINIC_CORE_LOWER_ERROR;
    }
    if (result_value != NULL) {
        *result_value = stored_value;
    }
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_assignment(MinicCoreLowerContext *context,
                                             const MinicStatement *statement) {
    MinicExpressionId source_id;
    MinicExpressionId target_id;

    if (statement == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    target_id = statement->target_expression;
    source_id = statement->expression;
    return lower_assignment_pair(context, target_id, source_id, statement->span, NULL);
}

/* M102_UNSIGNED_BIT_FIELD_SCALAR_UPDATE / M154_SIGNED_BIT_FIELD_UPDATE_OWNER:
   prefix/postfix ++/-- on an integer bit-field cannot use the ordinary
   addressable-lvalue update path.  Keep the allocation-unit RMW unsigned,
   but reconstruct signed field values from their declared width before
   promotion and after truncating the updated value. */
static MinicCoreLowerStatus lower_integer_bit_field_update(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    const MinicExpression *operand,
    bool increment,
    bool prefix,
    MinicCoreValueId *value_id) {
    const MinicExpression *base;
    const MinicRecord *record;
    const MinicRecordField *field;
    MinicCoreInstruction instruction;
    MinicCoreLowerStatus status;
    MinicCoreValueId address;
    MinicCoreValueId base_value;
    MinicCoreValueId constant;
    MinicCoreValueId current_field;
    MinicCoreValueId current_promoted;
    MinicCoreValueId current_storage;
    MinicCoreValueId field_storage;
    MinicCoreValueId merged;
    MinicCoreValueId shifted_current;
    MinicCoreValueId updated_promoted;
    MinicCoreValueId updated_value;
    MinicType base_value_type;
    MinicType expression_value_type;
    MinicType promoted_type;
    MinicType record_type;
    MinicType storage_access_type;
    MinicType storage_type;
    MinicType value_type;
    size_t byte_offset;
    size_t bit_offset;
    unsigned int storage_width;
    uint64_t clear_mask;
    uint64_t field_mask;
    uint64_t low_mask;
    uint64_t storage_mask;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || operand == NULL || value_id == NULL ||
        context->target == NULL || operand->kind != MINIC_EXPRESSION_MEMBER ||
        operand->value_category != MINIC_VALUE_LVALUE || minic_type_is_const(operand->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    record = minic_c0_program_record(context->body->program, operand->value.member.record_id);
    field = minic_c0_record_field(record, operand->value.member.field_index);
    base = minic_c0_program_expression(context->body->program, operand->value.member.base);
    if (record == NULL || field == NULL || !field->is_bit_field || base == NULL ||
        field->bit_width == 0U ||
        !minic_type_unqualified(operand->type, &value_type) ||
        !minic_type_is_integer(value_type) ||
        (!core_unsigned_bit_field_semantic_type(context, value_type) &&
         !minic_type_is_signed_integer(value_type)) ||
        minic_type_is_bool_integer(value_type) ||
        !minic_type_unqualified(expression->type, &expression_value_type) ||
        !minic_type_equal(expression_value_type, value_type) ||
        !minic_target_info_integer_promotion_for_program(
            context->target, context->body->program, value_type, &promoted_type) ||
        !core_bit_field_storage_type(
            context, value_type, &storage_type, &storage_width) ||
        storage_width == 0U || storage_width > 64U || field->bit_width > storage_width ||
        !minic_data_layout_record_field_layout(core_data_layout(context),
                                               context->body->program,
                                               record,
                                               operand->value.member.field_index,
                                               &byte_offset,
                                               &bit_offset) ||
        bit_offset + field->bit_width > storage_width ||
        !core_scalar_expression_value_type(context->body, base, &base_value_type) ||
        !minic_type_is_pointer(base_value_type) ||
        !minic_type_pointee(base_value_type, &record_type) ||
        !minic_type_is_record(record_type) ||
        record_type.record_id != operand->value.member.record_id) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    (void)byte_offset;

    storage_access_type = storage_type;
    if (minic_type_is_volatile(operand->type) &&
        !minic_type_add_volatile(storage_access_type, &storage_access_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = lower_expression(context, operand->value.member.base, &base_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (base_value >= context->function->value_count ||
        !minic_type_equal(context->function->values[base_value].type, base_value_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = append_field_address(context,
                                  operand->span,
                                  base_value,
                                  operand->value.member.record_id,
                                  operand->value.member.field_index,
                                  storage_access_type,
                                  &address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
    instruction.span = operand->span;
    instruction.type = storage_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.load.address = address;
    instruction.value.load.is_volatile = minic_type_is_volatile(operand->type);
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &current_storage)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    shifted_current = current_storage;
    if (bit_offset != 0U) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = operand->span;
        instruction.type = minic_type_unsigned_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = (int64_t)bit_offset;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
        instruction.span = operand->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = shifted_current;
        instruction.value.binary.right = constant;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &shifted_current)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    low_mask = field->bit_width == 64U
                   ? UINT64_MAX
                   : ((UINT64_C(1) << field->bit_width) - UINT64_C(1));
    if (field->bit_width < storage_width) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = operand->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        (void)memcpy(&instruction.value.integer_value, &low_mask, sizeof(low_mask));
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
        instruction.span = operand->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = shifted_current;
        instruction.value.binary.right = constant;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &shifted_current)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    current_field = shifted_current;
    if (!minic_type_equal(storage_type, value_type)) {
        status = append_integer_conversion(
            context, operand->span, value_type, current_field, &current_field);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    /* M154_SIGNED_BIT_FIELD_UPDATE_OWNER: the extracted storage bits are an
       unsigned bit pattern.  Reconstruct the signed field value before integer
       promotion, matching M103 signed bit-field reads and preserving postfix
       result semantics. */
    if (minic_type_is_signed_integer(value_type) && field->bit_width < storage_width) {
        MinicCoreValueId sign_shift;
        uint64_t sign_shift_bits = (uint64_t)(storage_width - field->bit_width);

        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = operand->span;
        instruction.type = minic_type_unsigned_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        (void)memcpy(&instruction.value.integer_value, &sign_shift_bits, sizeof(sign_shift_bits));
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &sign_shift)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
        instruction.span = operand->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current_field;
        instruction.value.binary.right = sign_shift;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &current_field)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
        instruction.span = operand->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current_field;
        instruction.value.binary.right = sign_shift;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &current_field)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    status = append_integer_conversion(
        context, operand->span, promoted_type, current_field, &current_promoted);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.span = expression->span;
    instruction.type = promoted_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.integer_value = 1;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &constant)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = increment ? MINIC_CORE_INSTRUCTION_INTEGER_ADD
                                 : MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
    instruction.span = expression->span;
    instruction.type = promoted_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.binary.left = current_promoted;
    instruction.value.binary.right = constant;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &updated_promoted)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = append_integer_conversion(
        context, expression->span, value_type, updated_promoted, &updated_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    field_storage = updated_value;
    if (!minic_type_equal(value_type, storage_type)) {
        status = append_integer_conversion(
            context, expression->span, storage_type, field_storage, &field_storage);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    if (field->bit_width < storage_width) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        (void)memcpy(&instruction.value.integer_value, &low_mask, sizeof(low_mask));
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
        instruction.span = expression->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = field_storage;
        instruction.value.binary.right = constant;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &field_storage)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    /* Prefix update yields the value actually stored in the bit-field, not an
       untruncated arithmetic temporary.  Rebuild that value from the masked
       storage bits; signed fields then use the same width sign extension. */
    if (minic_type_equal(storage_type, value_type)) {
        updated_value = field_storage;
    } else {
        status = append_integer_conversion(
            context, expression->span, value_type, field_storage, &updated_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    if (minic_type_is_signed_integer(value_type) && field->bit_width < storage_width) {
        MinicCoreValueId sign_shift;
        uint64_t sign_shift_bits = (uint64_t)(storage_width - field->bit_width);

        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_unsigned_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        (void)memcpy(&instruction.value.integer_value, &sign_shift_bits, sizeof(sign_shift_bits));
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &sign_shift)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
        instruction.span = expression->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = updated_value;
        instruction.value.binary.right = sign_shift;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &updated_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
        instruction.span = expression->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = updated_value;
        instruction.value.binary.right = sign_shift;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &updated_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    if (bit_offset != 0U) {
        uint64_t shift = (uint64_t)bit_offset;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        (void)memcpy(&instruction.value.integer_value, &shift, sizeof(shift));
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
        instruction.span = expression->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = field_storage;
        instruction.value.binary.right = constant;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &field_storage)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    field_mask = low_mask << bit_offset;
    storage_mask = storage_width == 64U
                       ? UINT64_MAX
                       : ((UINT64_C(1) << storage_width) - UINT64_C(1));
    clear_mask = (~field_mask) & storage_mask;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.span = expression->span;
    instruction.type = storage_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    (void)memcpy(&instruction.value.integer_value, &clear_mask, sizeof(clear_mask));
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &constant)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
    instruction.span = expression->span;
    instruction.type = storage_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.binary.left = current_storage;
    instruction.value.binary.right = constant;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &merged)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
    instruction.span = expression->span;
    instruction.type = storage_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.binary.left = merged;
    instruction.value.binary.right = field_storage;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &merged)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = expression->span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address;
    instruction.value.store.stored_value = merged;
    instruction.value.store.is_volatile = minic_type_is_volatile(operand->type);
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &instruction)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    *value_id = prefix ? updated_value : current_field;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_scalar_update(MinicCoreLowerContext *context,
                                                const MinicExpression *expression,
                                                MinicCoreValueId *value_id) {
    const MinicExpression *operand;
    MinicCoreInstruction instruction;
    MinicCoreValueId address;
    MinicCoreValueId current;
    MinicCoreValueId delta;
    MinicCoreValueId one;
    MinicCoreValueId updated;
    MinicCoreLowerStatus status;
    MinicType expression_value_type;
    MinicType stored_type;
    bool increment;
    bool prefix;

    /* M56_PREFIX_POSTFIX_SCALAR_UPDATE: both forms perform the same single
       load/update/store. Only the expression result differs: prefix yields the
       updated value, postfix yields the prior value. */
    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || value_id == NULL ||
        expression->kind != MINIC_EXPRESSION_UNARY ||
        (expression->value.unary.operator_kind != MINIC_UNARY_POST_INCREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_POST_DECREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_PRE_INCREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_PRE_DECREMENT)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    increment = expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
                expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT;
    prefix = expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT;
    operand = minic_c0_program_expression(context->body->program, expression->value.unary.operand);
    /* M63_QUALIFIED_SCALAR_UPDATE_VALUE: the memory operand may be qualified
       (notably volatile), while the computed prefix/postfix value transported
       by Core is an ordinary unqualified scalar. Preserve qualifiers solely on
       the load/store effects and compare the expression's value type after
       unqualification. */
    if (operand == NULL || operand->value_category != MINIC_VALUE_LVALUE ||
        !core_memory_scalar_type(operand->type) || minic_type_is_const(operand->type) ||
        !minic_type_unqualified(operand->type, &stored_type) ||
        !minic_type_unqualified(expression->type, &expression_value_type) ||
        !minic_type_equal(expression_value_type, stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (minic_type_is_integer(stored_type) && minic_type_is_bool_integer(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (operand->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicRecord *update_record =
            minic_c0_program_record(context->body->program, operand->value.member.record_id);
        const MinicRecordField *update_field =
            minic_c0_record_field(update_record, operand->value.member.field_index);

        if (update_field != NULL && update_field->is_bit_field) {
            return lower_integer_bit_field_update(
                context, expression, operand, increment, prefix, value_id);
        }
    }
    status = lower_address(context, expression->value.unary.operand, &address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
    instruction.span = expression->span;
    instruction.type = stored_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.load.address = address;
    instruction.value.load.is_volatile = minic_type_is_volatile(operand->type);
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &current)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    if (minic_type_is_integer(stored_type)) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = 1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &one)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        delta = one;
        if (!increment) {
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_NEGATE;
            instruction.span = expression->span;
            instruction.type = stored_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.operand = one;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &delta)) {
                return MINIC_CORE_LOWER_ERROR;
            }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
        instruction.span = expression->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current;
        instruction.value.binary.right = delta;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &updated)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    } else if (minic_type_is_pointer(stored_type)) {
        size_t element_size;

        if (!minic_c0_pointer_arithmetic_element_size(
                context->body->program, core_data_layout(context), stored_type, &element_size)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = increment ? 1 : -1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &delta)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        instruction.span = expression->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.pointer_offset.base = current;
        instruction.value.pointer_offset.index = delta;
        instruction.value.pointer_offset.element_size = element_size;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &updated)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    } else {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = expression->span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address;
    instruction.value.store.stored_value = updated;
    instruction.value.store.is_volatile = minic_type_is_volatile(operand->type);
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &instruction)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    *value_id = prefix ? updated : current;
    return MINIC_CORE_LOWER_OK;
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

static MinicCoreLowerStatus lower_expression_statement(MinicCoreLowerContext *context,
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
        return lower_assignment_pair(context,
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
        status = lower_expression_statement(context, &discarded);
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
        return lower_expression_statement(context, &discarded);
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

            status = lower_record_materialized_address(
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

        status = lower_record_materialized_address(
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

            return lower_direct_record_call_object(context, expression, &discarded_object);
        }
        {
            MinicCoreValueId discarded_value;
            MinicCoreLowerStatus status;

            status = lower_expression(context, statement->expression, &discarded_value);
            return core_trace_expression_statement_status(context, expression, "call", status);
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
        status = lower_block(context, statement_block, &statement_expression_terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (statement_expression_terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return lower_record_materialized_address(
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
            lower_scalar_update(context, expression, &discarded_value));
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
            block_status = lower_block(context, statement_block, &statement_expression_terminated);
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
        lower_assignment_pair(context, target_id, source_id, expression->span, NULL));
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
            status = lower_scalar_assignment_value(context,
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
                status = lower_direct_record_call_object(
                    context, expression, &terminator.return_object);
            } else if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
                       minic_type_is_record(expression->type)) {
                status = lower_record_conditional_object(
                    context, expression, &terminator.return_object);
            } else if (minic_c0_record_value_is_address_backed(
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
            status = spill_scalar_value(context, statement->span,
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
            status = reload_scalar_value(context, statement->span,
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

static MinicCoreLowerStatus set_branch(MinicCoreLowerContext *context,
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

static MinicCoreLowerStatus lower_condition_branch(MinicCoreLowerContext *context,
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
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        const MinicExpression *operand;

        operand =
            minic_c0_program_expression(context->body->program, expression->value.unary.operand);
        if (operand != NULL &&
            (minic_type_is_integer(operand->type) || minic_type_is_pointer(operand->type))) {
            return lower_condition_branch(
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
            return lower_condition_branch(
                context, expression->value.unary.operand, span, when_true, when_false);
        }
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND) {
        MinicCoreBlockId right_block;

        if (!minic_core_function_add_block(context->function, &right_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_condition_branch(
            context, expression->value.binary.left, span, right_block, when_false);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        context->block_id = right_block;
        return lower_condition_branch(
            context, expression->value.binary.right, span, when_true, when_false);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {
        MinicCoreBlockId right_block;

        if (!minic_core_function_add_block(context->function, &right_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_condition_branch(
            context, expression->value.binary.left, span, when_true, right_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        context->block_id = right_block;
        return lower_condition_branch(
            context, expression->value.binary.right, span, when_true, when_false);
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
    bool else_terminated;
    bool needs_merge;
    bool then_terminated;

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

    condition_block = context->block_id;
    if (!minic_core_function_add_block(context->function, &then_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    else_block = MINIC_CORE_BLOCK_INVALID;
    if (else_source != NULL && !minic_core_function_add_block(context->function, &else_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    context->block_id = then_block;
    status = lower_block(context, then_source, &then_terminated);
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
    then_continuation_block = context->block_id;
    else_continuation_block = MINIC_CORE_BLOCK_INVALID;
    else_terminated = false;
    if (else_source != NULL) {
        context->block_id = else_block;
        status = lower_block(context, else_source, &else_terminated);
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
    }

    needs_merge = !then_terminated || else_source == NULL || !else_terminated;
    merge_block = MINIC_CORE_BLOCK_INVALID;
    if (needs_merge) {
        if (!minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!then_terminated) {
            status = set_branch(context, then_continuation_block, statement->span, merge_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        if (else_source != NULL && !else_terminated) {
            status = set_branch(context, else_continuation_block, statement->span, merge_block);
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
    status = lower_condition_branch(
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
        return status;
    }
    context->block_id = continuation_block;
    *terminated = !needs_merge;
    return MINIC_CORE_LOWER_OK;
}

static bool internal_while_label_pair(const MinicStatement *label, const MinicStatement *loop) {
    bool same_begin;

    if (label == NULL || loop == NULL) {
        return false;
    }
    same_begin = label->span.begin.offset == loop->span.begin.offset &&
                 label->span.begin.line == loop->span.begin.line &&
                 label->span.begin.column == loop->span.begin.column;
    return label->kind == MINIC_STATEMENT_LABEL && loop->kind == MINIC_STATEMENT_WHILE &&
           same_begin && label->target_expression == MINIC_EXPRESSION_INVALID &&
           label->expression == MINIC_EXPRESSION_INVALID &&
           label->target_statement == MINIC_STATEMENT_INVALID;
}

static bool source_position_equal(MinicSourcePosition left, MinicSourcePosition right) {
    return left.offset == right.offset && left.line == right.line && left.column == right.column;
}

static bool core_cleanup_edge_is_empty(const MinicStatement *statement) {
    return statement != NULL &&
           statement->cleanup_context == statement->cleanup_stop_context;
}

/* M121_SELECTIVE_DO_WHILE_ZERO_EXIT_CFG: parse_do_while lowers source
   `do BODY while (0)` into a synthetic while(1) tail. Preserve the historical
   flattening path when BODY has no control transfer to this loop; only build a
   real exit block when a source break/continue actually needs loop ownership. */
static bool normalized_do_while_zero_body(const MinicCoreLowerContext *context,
                                          const MinicStatement *loop,
                                          const MinicBlock *body,
                                          MinicBlock *single_iteration_body,
                                          MinicStatementId *continue_label_statement) {
    const MinicExpression *loop_condition;
    const MinicExpression *negated_condition;
    const MinicExpression *source_condition;
    const MinicStatement *continue_label;
    const MinicStatement *condition_check;
    const MinicStatement *break_statement;
    const MinicBlock *break_block;
    MinicStatementId continue_id;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        loop == NULL || body == NULL || single_iteration_body == NULL ||
        continue_label_statement == NULL || body->statement_count < 2U) {
        return false;
    }
    loop_condition = minic_c0_program_expression(context->body->program, loop->expression);
    continue_id = body->statements[body->statement_count - 2U];
    continue_label = minic_c0_program_statement(context->body->program, continue_id);
    condition_check = minic_c0_program_statement(
        context->body->program, body->statements[body->statement_count - 1U]);
    if (loop_condition == NULL || loop_condition->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(loop_condition->type) || loop_condition->value.integer_value != 1 ||
        continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin) ||
        condition_check == NULL || condition_check->kind != MINIC_STATEMENT_IF ||
        condition_check->expression == MINIC_EXPRESSION_INVALID ||
        condition_check->then_block == MINIC_BLOCK_INVALID ||
        condition_check->else_block != MINIC_BLOCK_INVALID ||
        !source_position_equal(condition_check->span.begin, loop->span.begin)) {
        return false;
    }
    negated_condition =
        minic_c0_program_expression(context->body->program, condition_check->expression);
    if (negated_condition == NULL || negated_condition->kind != MINIC_EXPRESSION_UNARY ||
        negated_condition->value.unary.operator_kind != MINIC_UNARY_LOGICAL_NOT) {
        return false;
    }
    source_condition = minic_c0_program_expression(
        context->body->program, negated_condition->value.unary.operand);
    if (source_condition == NULL || source_condition->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(source_condition->type) || source_condition->value.integer_value != 0) {
        return false;
    }
    break_block = minic_c0_program_block(context->body->program, condition_check->then_block);
    if (break_block == NULL || break_block->statement_count != 1U) {
        return false;
    }
    break_statement = minic_c0_program_statement(context->body->program, break_block->statements[0]);
    if (break_statement == NULL || break_statement->kind != MINIC_STATEMENT_BREAK ||
        !core_cleanup_edge_is_empty(break_statement) ||
        !source_position_equal(break_statement->span.begin, loop->span.begin)) {
        return false;
    }

    *single_iteration_body = *body;
    single_iteration_body->statement_count -= 2U;
    *continue_label_statement = continue_id;
    return true;
}

static bool normalized_do_while_block_needs_exit(
    const MinicCoreLowerContext *context,
    const MinicBlock *block,
    MinicStatementId continue_label_statement,
    bool break_targets_outer_loop) {
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        block == NULL) {
        return true;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        const MinicStatement *statement = minic_c0_program_statement(
            context->body->program, block->statements[index]);
        if (statement == NULL) {
            return true;
        }
        if (break_targets_outer_loop && statement->kind == MINIC_STATEMENT_BREAK) {
            return true;
        }
        if (statement->kind == MINIC_STATEMENT_GOTO &&
            statement->target_statement == continue_label_statement) {
            return true;
        }
        if (statement->kind == MINIC_STATEMENT_IF ||
            statement->kind == MINIC_STATEMENT_WHILE ||
            statement->kind == MINIC_STATEMENT_SWITCH) {
            const MinicBlock *then_block = statement->then_block == MINIC_BLOCK_INVALID
                                                ? NULL
                                                : minic_c0_program_block(
                                                      context->body->program,
                                                      statement->then_block);
            const MinicBlock *else_block = statement->else_block == MINIC_BLOCK_INVALID
                                                ? NULL
                                                : minic_c0_program_block(
                                                      context->body->program,
                                                      statement->else_block);
            bool nested_break_targets_outer =
                statement->kind == MINIC_STATEMENT_IF ? break_targets_outer_loop : false;
            if (statement->then_block != MINIC_BLOCK_INVALID &&
                (then_block == NULL || normalized_do_while_block_needs_exit(
                                           context,
                                           then_block,
                                           continue_label_statement,
                                           nested_break_targets_outer))) {
                return true;
            }
            if (statement->else_block != MINIC_BLOCK_INVALID &&
                (else_block == NULL || normalized_do_while_block_needs_exit(
                                           context,
                                           else_block,
                                           continue_label_statement,
                                           nested_break_targets_outer))) {
                return true;
            }
        }
    }
    return false;
}

/* M78_OMITTED_FOR_CONDITION: parse_for represents a missing source
   condition as MINIC_EXPRESSION_INVALID and appends its synthetic continue
   label to the loop body. Recognize the no-update variant explicitly so Core
   can distinguish `for (;;)` from an invalid source while statement. */
/* M159_CONSTANT_TRUE_DO_WHILE_REACHABILITY_OWNER: parse_do_while lowers
   every source do/while into an outer synthetic while(1), followed inside its
   body by a synthetic continue label and `if (!source_condition) break`.
   When source_condition is a compile-time nonzero integer and the original
   body contains no break/continue edge that needs those synthetic nodes, that
   tail is unreachable control metadata.  Strip it from the executable view so
   the ordinary constant-true while CFG has no false exit predecessor. */
static bool normalized_do_while_true_body(const MinicCoreLowerContext *context,
                                          const MinicStatement *loop,
                                          const MinicBlock *body,
                                          MinicBlock *iteration_body,
                                          MinicStatementId *continue_label_statement) {
    const MinicExpression *loop_condition;
    const MinicExpression *negated_condition;
    const MinicExpression *source_condition;
    const MinicStatement *continue_label;
    const MinicStatement *condition_check;
    const MinicStatement *break_statement;
    const MinicBlock *break_block;
    MinicConstValue condition_value;
    MinicStatementId continue_id;
    bool is_zero;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || loop == NULL || body == NULL || iteration_body == NULL ||
        continue_label_statement == NULL || body->statement_count < 2U) {
        return false;
    }
    loop_condition = minic_c0_program_expression(context->body->program, loop->expression);
    continue_id = body->statements[body->statement_count - 2U];
    continue_label = minic_c0_program_statement(context->body->program, continue_id);
    condition_check = minic_c0_program_statement(
        context->body->program, body->statements[body->statement_count - 1U]);
    if (loop_condition == NULL || loop_condition->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(loop_condition->type) || loop_condition->value.integer_value != 1 ||
        continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin) ||
        condition_check == NULL || condition_check->kind != MINIC_STATEMENT_IF ||
        condition_check->expression == MINIC_EXPRESSION_INVALID ||
        condition_check->then_block == MINIC_BLOCK_INVALID ||
        condition_check->else_block != MINIC_BLOCK_INVALID ||
        !source_position_equal(condition_check->span.begin, loop->span.begin)) {
        return false;
    }
    negated_condition =
        minic_c0_program_expression(context->body->program, condition_check->expression);
    if (negated_condition == NULL || negated_condition->kind != MINIC_EXPRESSION_UNARY ||
        negated_condition->value.unary.operator_kind != MINIC_UNARY_LOGICAL_NOT) {
        return false;
    }
    source_condition = minic_c0_program_expression(
        context->body->program, negated_condition->value.unary.operand);
    if (source_condition == NULL || !minic_type_is_integer(source_condition->type) ||
        !minic_const_eval_integer(context->body->program,
                                 context->target,
                                 negated_condition->value.unary.operand,
                                 &condition_value) ||
        !minic_const_value_is_zero(context->body->program,
                                   context->target,
                                   &condition_value,
                                   &is_zero) ||
        is_zero) {
        return false;
    }
    break_block = minic_c0_program_block(context->body->program, condition_check->then_block);
    if (break_block == NULL || break_block->statement_count != 1U) {
        return false;
    }
    break_statement = minic_c0_program_statement(context->body->program, break_block->statements[0]);
    if (break_statement == NULL || break_statement->kind != MINIC_STATEMENT_BREAK ||
        !core_cleanup_edge_is_empty(break_statement) ||
        !source_position_equal(break_statement->span.begin, loop->span.begin)) {
        return false;
    }

    /* The source condition is proven nonzero, so the parser's trailing
       `if (!condition) break` is unreachable. Preserve source continue
       semantics by binding the synthetic continue label to the real loop
       condition block instead of retaining the synthetic tail. Source break
       statements remain ordinary loop-owned edges to exit_block. */
    *iteration_body = *body;
    iteration_body->statement_count -= 2U;
    *continue_label_statement = continue_id;
    return true;
}

static bool normalized_for_continue_tail(const MinicCoreLowerContext *context,
                                         const MinicStatement *loop,
                                         const MinicBlock *body,
                                         MinicBlock *iteration_body) {
    const MinicStatement *continue_label;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        loop == NULL || body == NULL || iteration_body == NULL || body->statement_count < 1U) {
        return false;
    }
    continue_label = minic_c0_program_statement(
        context->body->program, body->statements[body->statement_count - 1U]);
    if (continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin)) {
        return false;
    }
    /* A normalized for-loop's synthetic continue label is part of the
       iteration body only when reaching it does not cross a cleanup lifetime.
       Cleanup-bearing scopes keep the historical tail shape until Core owns
       cleanup transitions explicitly. */
    *iteration_body = *body;
    if (!core_cleanup_edge_is_empty(continue_label)) {
        iteration_body->statement_count -= 1U;
    }
    return true;
}

static bool normalized_for_update_tail(const MinicCoreLowerContext *context,
                                       const MinicStatement *loop,
                                       const MinicBlock *body,
                                       MinicBlock *iteration_body,
                                       const MinicStatement **update_statement) {
    const MinicStatement *continue_label;
    const MinicStatement *update;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        loop == NULL || body == NULL || iteration_body == NULL || update_statement == NULL ||
        body->statement_count < 2U) {
        return false;
    }
    continue_label = minic_c0_program_statement(context->body->program,
                                                body->statements[body->statement_count - 2U]);
    update = minic_c0_program_statement(context->body->program,
                                        body->statements[body->statement_count - 1U]);
    if (continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin) || update == NULL ||
        update->kind != MINIC_STATEMENT_EXPRESSION || !core_cleanup_edge_is_empty(update) ||
        update->expression == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    /* The update is always outside the lowered iteration body.  Preserve the
       preceding synthetic continue label only for a zero-distance cleanup
       edge; otherwise leave cleanup-bearing for scopes on the established
       fail-closed path. */
    *iteration_body = *body;
    iteration_body->statement_count -=
        core_cleanup_edge_is_empty(continue_label) ? 1U : 2U;
    *update_statement = update;
    return true;
}

/* M132_UNBOUNDED_FOR_TERMINATION: structured control-flow reachability is
   the owner of whether a synthetic loop exit can fall through. In particular,
   an omitted-condition GNU/C `for (;;)` has no natural edge to its exit block;
   only an explicit break may make that exit reachable. */
static bool core_block_has_predecessor(const MinicCoreFunction *function,
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
        statement->kind != MINIC_STATEMENT_WHILE || !core_cleanup_edge_is_empty(statement) ||
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
    if (normalized_do_while_zero_body(context,
                                      statement,
                                      body_source,
                                      &normalized_do_while_body,
                                      &normalized_do_while_continue)) {
        if (!normalized_do_while_block_needs_exit(context,
                                                  &normalized_do_while_body,
                                                  normalized_do_while_continue,
                                                  true)) {
            /* Keep the previously proven flattening semantics when BODY has no
               transfer owned by this loop. This avoids manufacturing CFG state
               for the many effect-only do-while(0) macros already accepted. */
            status = lower_block(context, &normalized_do_while_body, &body_terminated);
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
            status = lower_block(context, &normalized_do_while_body, &body_terminated);
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
                status = set_branch(
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
    if (normalized_for_update_tail(
            context, statement, body_source, &normalized_for_body, &for_update)) {
        iteration_source = &normalized_for_body;
        normalized_for = true;
    } else if (normalized_for_continue_tail(
                   context, statement, body_source, &normalized_for_body)) {
        iteration_source = &normalized_for_body;
        normalized_for = true;
    }
    if (!normalized_for &&
        normalized_do_while_true_body(context,
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
    /* M149_PARSER_TAIL_CONTINUE_PROVENANCE_OWNER: normalized_for_update_tail
       and normalized_for_continue_tail have already proven the exact parser
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
    status = set_branch(context, preheader_block, statement->span, condition_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = condition_block;
    if (statement->expression == MINIC_EXPRESSION_INVALID ||
        core_loop_condition_is_constant_true(context, statement->expression)) {
        /* C defines an omitted for-condition as true, and a proven nonzero
           integer constant has the same CFG reachability. Keep an explicit
           Core condition block so break/backedge ownership remains identical. */
        status = set_branch(context, condition_block, statement->span, body_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        /* M84_SHARED_LOOP_CONDITION_BRANCH: if/while/normalized-for share one
           scalar-condition owner. This admits pointer truth values and keeps
           !/&&/|| short-circuit CFG construction out of the loop lowering. */
        status = lower_condition_branch(context,
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
    status = lower_block(context, iteration_source, &body_terminated);
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
            status = set_branch(context, context->block_id, statement->span, update_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        context->block_id = update_block;
        status = lower_expression_statement(context, for_update);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (context->block_id < context->function->block_count &&
            context->function->blocks[context->block_id].has_terminator) {
            body_terminated = true;
        } else {
            body_terminated = false;
            status = set_branch(context, context->block_id, statement->span, condition_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    } else {
        if (!body_terminated && for_update != NULL) {
            status = lower_expression_statement(context, for_update);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (context->block_id < context->function->block_count &&
                context->function->blocks[context->block_id].has_terminator) {
                body_terminated = true;
            }
        }
        if (!body_terminated) {
            status = set_branch(context, context->block_id, statement->span, condition_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    }
    context->block_id = exit_block;
    /* M133_CONSTANT_TRUE_LOOP_REACHABILITY: reachability, not syntax spelling,
       owns loop fallthrough. Normal variable/false-capable conditions already
       contribute a false edge; omitted and constant-true conditions do not. */
    if (!core_block_has_predecessor(context->function, exit_block)) {
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

#define MINIC_CORE_SWITCH_LABEL_LIMIT 512U

typedef struct MinicCoreSwitchLabel {
    size_t source_index;
    const MinicStatement *statement;
    MinicCoreBlockId body_block;
    MinicCoreBlockId test_block;
} MinicCoreSwitchLabel;

static MinicCoreLowerStatus append_switch_integer_constant(MinicCoreLowerContext *context,
                                                           MinicSourceSpan span,
                                                           MinicType type,
                                                           int64_t value,
                                                           MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;

    if (context == NULL || context->function == NULL || value_id == NULL ||
        !minic_type_is_integer(type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.span = span;
    instruction.type = type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.integer_value = value;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus set_switch_conditional_branch(MinicCoreLowerContext *context,
                                                          MinicSourceSpan span,
                                                          MinicCoreValueId condition,
                                                          MinicCoreBlockId when_true,
                                                          MinicCoreBlockId when_false) {
    MinicCoreTerminator terminator;

    if (context == NULL || context->function == NULL ||
        condition >= context->function->value_count ||
        !minic_type_is_integer(context->function->values[condition].type) ||
        when_true == MINIC_CORE_BLOCK_INVALID || when_false == MINIC_CORE_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    terminator.span = span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.conditional.condition = condition;
    terminator.conditional.when_true = when_true;
    terminator.conditional.when_false = when_false;
    return minic_core_function_set_terminator(context->function, context->block_id, &terminator)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_switch_case_dispatch(MinicCoreLowerContext *context,
                                                       const MinicStatement *case_statement,
                                                       MinicType selector_type,
                                                       MinicCoreObjectId selector_object,
                                                       MinicCoreBlockId body_target,
                                                       MinicCoreBlockId next_target) {
    const MinicExpression *lower_expression;
    const MinicExpression *upper_expression;
    MinicConstValue lower_constant;
    MinicConstValue lower_converted;
    MinicConstValue upper_constant;
    MinicConstValue upper_converted;
    MinicCoreInstruction instruction;
    MinicCoreValueId bound;
    MinicCoreValueId comparison;
    MinicCoreValueId selector;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || case_statement == NULL ||
        case_statement->kind != MINIC_STATEMENT_CASE ||
        case_statement->expression == MINIC_EXPRESSION_INVALID ||
        !minic_type_is_integer(selector_type) || body_target == MINIC_CORE_BLOCK_INVALID ||
        next_target == MINIC_CORE_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    lower_expression =
        minic_c0_program_expression(context->body->program, case_statement->expression);
    if (lower_expression == NULL || !minic_type_is_integer(lower_expression->type) ||
        !minic_const_eval_integer(context->body->program,
                                  context->target,
                                  case_statement->expression,
                                  &lower_constant) ||
        !minic_const_value_convert_integer(context->body->program,
                                           context->target,
                                           &lower_constant,
                                           selector_type,
                                           &lower_converted)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = reload_scalar_value(
        context, case_statement->span, selector_type, selector_object, &selector);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    {
        int64_t lower_value;

        lower_value = 0;
        (void)memcpy(&lower_value, &lower_converted.bits, sizeof(lower_value));
        status = append_switch_integer_constant(context,
                                                lower_expression->span,
                                                selector_type,
                                                lower_value,
                                                &bound);
    }
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    if (case_statement->target_expression == MINIC_EXPRESSION_INVALID) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        instruction.span = case_statement->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = selector;
        instruction.value.binary.right = bound;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &comparison)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return set_switch_conditional_branch(
            context, case_statement->span, comparison, body_target, next_target);
    }

    upper_expression =
        minic_c0_program_expression(context->body->program, case_statement->target_expression);
    if (upper_expression == NULL || !minic_type_is_integer(upper_expression->type) ||
        !minic_const_eval_integer(context->body->program,
                                  context->target,
                                  case_statement->target_expression,
                                  &upper_constant) ||
        !minic_const_value_convert_integer(context->body->program,
                                           context->target,
                                           &upper_constant,
                                           selector_type,
                                           &upper_converted)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    {
        MinicCoreBlockId upper_test_block;

        if (!minic_core_function_add_block(context->function, &upper_test_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = case_statement->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = selector;
        instruction.value.binary.right = bound;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &comparison)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_switch_conditional_branch(
            context, case_statement->span, comparison, next_target, upper_test_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = upper_test_block;
        {
            int64_t upper_value;

            upper_value = 0;
            (void)memcpy(&upper_value, &upper_converted.bits, sizeof(upper_value));
            status = append_switch_integer_constant(context,
                                                    upper_expression->span,
                                                    selector_type,
                                                    upper_value,
                                                    &bound);
        }
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(
            context, case_statement->span, selector_type, selector_object, &selector);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = case_statement->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = bound;
        instruction.value.binary.right = selector;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &comparison)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return set_switch_conditional_branch(
            context, case_statement->span, comparison, next_target, body_target);
    }
}

static bool core_switch_label_has_function_reentry(
    const MinicCoreLowerContext *context, MinicStatementId label_id) {
    const MinicC0Program *program;
    const MinicStatement *label;
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL) {
        return false;
    }
    program = context->body->program;
    label = minic_c0_program_statement(program, label_id);
    if (label == NULL || label->kind != MINIC_STATEMENT_LABEL) {
        return false;
    }
    for (index = 0U; index < program->statement_count; ++index) {
        const MinicStatement *source;

        source = minic_c0_program_statement(program, index);
        if (source == NULL) {
            return false;
        }
        if (source->kind == MINIC_STATEMENT_GOTO &&
            source->expression == MINIC_EXPRESSION_INVALID &&
            source->target_statement == label_id) {
            return true;
        }
        if (source->kind == MINIC_STATEMENT_INLINE_ASM &&
            source->inline_asm_id < program->inline_asm_count) {
            const MinicInlineAsm *inline_asm = &program->inline_asms[source->inline_asm_id];
            size_t label_index;

            if (!inline_asm->is_goto) {
                continue;
            }
            for (label_index = 0U; label_index < inline_asm->label_count; ++label_index) {
                if (inline_asm->labels[label_index].target_statement == label_id) {
                    return true;
                }
            }
        }
    }
    for (index = 0U; index < program->expression_count; ++index) {
        const MinicExpression *expression = minic_c0_program_expression(program, index);

        if (expression == NULL) {
            return false;
        }
        if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS &&
            expression->value.label_statement_id == label_id) {
            return true;
        }
    }
    return false;
}

/* M176_SWITCH_POST_BREAK_LABEL_REENTRY: a direct break ends ordinary switch
   fallthrough, but a later ordinary C label remains a valid goto target. Keep
   that re-entry path separate from the case segment so break still reaches the
   synthetic switch exit and only a genuinely referenced label can revive the
   unreachable tail. */
static MinicCoreLowerStatus
lower_switch(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
    const MinicBlock *body;
    const MinicExpression *selector_expression;
    MinicCoreSwitchLabel labels[MINIC_CORE_SWITCH_LABEL_LIMIT];
    MinicCoreBlockId default_target;
    MinicCoreBlockId dispatch_target;
    MinicCoreBlockId exit_block;
    MinicCoreObjectId selector_object;
    MinicCoreValueId selector_normalized;
    MinicCoreValueId selector_source;
    MinicCoreLowerStatus status;
    MinicType selector_type;
    size_t case_count;
    size_t default_label;
    size_t first_case_label;
    size_t label_count;
    size_t source_index;
    bool all_segments_terminate;
    bool segment_breaks[MINIC_CORE_SWITCH_LABEL_LIMIT];
    bool segment_terminates[MINIC_CORE_SWITCH_LABEL_LIMIT];

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL || terminated == NULL ||
        statement->kind != MINIC_STATEMENT_SWITCH ||
        !core_cleanup_edge_is_empty(statement) ||
        statement->expression == MINIC_EXPRESSION_INVALID ||
        statement->then_block == MINIC_BLOCK_INVALID ||
        statement->else_block != MINIC_BLOCK_INVALID || context->target == NULL) {
        if (context != NULL && context->source_function != NULL && statement != NULL) {
            (void)fprintf(stderr,
                          "CORE_SWITCH_DETAIL function=%s gate=entry cleanup=%llu stop=%llu "
                          "expr=%u then=%u else=%u\n",
                          context->source_function->name,
                          (unsigned long long)statement->cleanup_context,
                          (unsigned long long)statement->cleanup_stop_context,
                          (unsigned)statement->expression,
                          (unsigned)statement->then_block,
                          (unsigned)statement->else_block);
        }
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    selector_expression =
        minic_c0_program_expression(context->body->program, statement->expression);
    body = minic_c0_program_block(context->body->program, statement->then_block);
    if (selector_expression == NULL || body == NULL ||
        !minic_type_is_integer(selector_expression->type) ||
        !minic_target_info_integer_promotion_for_program(
            context->target, context->body->program, selector_expression->type, &selector_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    case_count = 0U;
    default_label = SIZE_MAX;
    first_case_label = SIZE_MAX;
    label_count = 0U;
    for (source_index = 0U; source_index < body->statement_count; ++source_index) {
        const MinicStatement *source_statement;

        source_statement =
            minic_c0_program_statement(context->body->program, body->statements[source_index]);
        if (source_statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (source_statement->kind != MINIC_STATEMENT_CASE &&
            source_statement->kind != MINIC_STATEMENT_DEFAULT) {
            if (label_count == 0U) {
                (void)fprintf(stderr,
                              "CORE_SWITCH_DETAIL function=%s gate=prelabel source_index=%zu "
                              "kind=%d\n",
                              context->source_function->name,
                              source_index,
                              (int)source_statement->kind);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            continue;
        }
        if (!core_cleanup_edge_is_empty(source_statement) ||
            source_statement->then_block != MINIC_BLOCK_INVALID ||
            source_statement->else_block != MINIC_BLOCK_INVALID ||
            label_count >= MINIC_CORE_SWITCH_LABEL_LIMIT) {
            (void)fprintf(stderr,
                          "CORE_SWITCH_DETAIL function=%s gate=label source_index=%zu kind=%d "
                          "cleanup=%llu stop=%llu then=%u else=%u labels=%zu\n",
                          context->source_function->name,
                          source_index,
                          (int)source_statement->kind,
                          (unsigned long long)source_statement->cleanup_context,
                          (unsigned long long)source_statement->cleanup_stop_context,
                          (unsigned)source_statement->then_block,
                          (unsigned)source_statement->else_block,
                          label_count);
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        labels[label_count].source_index = source_index;
        labels[label_count].statement = source_statement;
        labels[label_count].body_block = MINIC_CORE_BLOCK_INVALID;
        labels[label_count].test_block = MINIC_CORE_BLOCK_INVALID;
        if (source_statement->kind == MINIC_STATEMENT_CASE) {
            if (source_statement->expression == MINIC_EXPRESSION_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (first_case_label == SIZE_MAX) {
                first_case_label = label_count;
            }
            case_count += 1U;
        } else {
            if (default_label != SIZE_MAX ||
                source_statement->expression != MINIC_EXPRESSION_INVALID ||
                source_statement->target_expression != MINIC_EXPRESSION_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
            default_label = label_count;
        }
        label_count += 1U;
    }

    status = lower_expression(context, statement->expression, &selector_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = append_integer_conversion(
        context, selector_expression->span, selector_type, selector_source, &selector_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = spill_scalar_value(
        context, selector_expression->span, selector_type, selector_normalized, &selector_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    if (!minic_core_function_add_block(context->function, &exit_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (source_index = 0U; source_index < label_count; ++source_index) {
        if (!minic_core_function_add_block(context->function, &labels[source_index].body_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (labels[source_index].statement->kind == MINIC_STATEMENT_CASE &&
            !minic_core_function_add_block(context->function, &labels[source_index].test_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    default_target = default_label == SIZE_MAX ? exit_block : labels[default_label].body_block;
    dispatch_target =
        first_case_label == SIZE_MAX ? default_target : labels[first_case_label].test_block;
    status = set_branch(context, context->block_id, statement->span, dispatch_target);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    if (case_count != 0U) {
        size_t label_index;

        for (label_index = 0U; label_index < label_count; ++label_index) {
            size_t next_label;
            MinicCoreBlockId next_target;

            if (labels[label_index].statement->kind != MINIC_STATEMENT_CASE) {
                continue;
            }
            next_target = default_target;
            for (next_label = label_index + 1U; next_label < label_count; ++next_label) {
                if (labels[next_label].statement->kind == MINIC_STATEMENT_CASE) {
                    next_target = labels[next_label].test_block;
                    break;
                }
            }
            context->block_id = labels[label_index].test_block;
            status = lower_switch_case_dispatch(context,
                                                labels[label_index].statement,
                                                selector_type,
                                                selector_object,
                                                labels[label_index].body_block,
                                                next_target);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    }

    /* M101_TERMINATING_SWITCH_FALLTHROUGH: termination is a property of the
       path starting at a label, not only of that label's immediate segment.
       Record each segment first, then prove fallthrough chains backwards. A
       break still reaches the synthetic exit and is therefore non-terminating. */
    (void)memset(segment_breaks, 0, sizeof(segment_breaks));
    (void)memset(segment_terminates, 0, sizeof(segment_terminates));
    for (source_index = 0U; source_index < label_count; ++source_index) {
        MinicBlock segment;
        MinicCoreBlockId fallthrough_target;
        size_t break_index;
        size_t reentry_index;
        size_t segment_begin;
        size_t segment_end;
        size_t scan;
        MinicCoreBlockId saved_break_target;
        bool segment_terminated;

        segment_begin = labels[source_index].source_index + 1U;
        segment_end = source_index + 1U < label_count ? labels[source_index + 1U].source_index
                                                      : body->statement_count;
        break_index = SIZE_MAX;
        reentry_index = SIZE_MAX;
        for (scan = segment_begin; scan < segment_end; ++scan) {
            const MinicStatement *segment_statement;

            segment_statement =
                minic_c0_program_statement(context->body->program, body->statements[scan]);
            if (segment_statement == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            /* A direct break terminates ordinary switch fallthrough. A later
               ordinary LABEL is reachable only when the function really owns
               a goto/asm-goto/label-address edge to it; split that tail into
               its own Core entry instead of reconnecting the break path. */
            if (break_index != SIZE_MAX &&
                segment_statement->kind != MINIC_STATEMENT_BREAK) {
                if (segment_statement->kind == MINIC_STATEMENT_LABEL &&
                    core_switch_label_has_function_reentry(
                        context, body->statements[scan])) {
                    reentry_index = scan;
                    break;
                }
                (void)fprintf(stderr,
                              "CORE_SWITCH_DETAIL function=%s gate=post-break "
                              "source_index=%zu scan=%zu kind=%d\n",
                              context->source_function->name,
                              source_index,
                              scan,
                              (int)segment_statement->kind);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            if (segment_statement->kind == MINIC_STATEMENT_BREAK) {
                if (!core_cleanup_edge_is_empty(segment_statement)) {
                    (void)fprintf(stderr,
                                  "CORE_SWITCH_DETAIL function=%s gate=break-cleanup "
                                  "source_index=%zu scan=%zu cleanup=%llu stop=%llu\n",
                                  context->source_function->name,
                                  source_index,
                                  scan,
                                  (unsigned long long)segment_statement->cleanup_context,
                                  (unsigned long long)segment_statement->cleanup_stop_context);
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                if (break_index == SIZE_MAX) {
                    break_index = scan;
                }
            }
        }

        context->block_id = labels[source_index].body_block;
        segment_terminated = false;
        segment = *body;
        segment.statements = body->statements + segment_begin;
        segment.statement_count =
            (break_index == SIZE_MAX ? segment_end : break_index) - segment_begin;
        segment.statement_capacity = segment.statement_count;
        if (segment.statement_count != 0U) {
            saved_break_target = context->break_target;
            context->break_target = exit_block;
            status = lower_block(context, &segment, &segment_terminated);
            context->break_target = saved_break_target;
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        segment_terminates[source_index] = segment_terminated;
        segment_breaks[source_index] = break_index != SIZE_MAX;
        if (segment_terminated) {
            continue;
        }
        if (break_index != SIZE_MAX) {
            fallthrough_target = exit_block;
        } else if (source_index + 1U < label_count) {
            fallthrough_target = labels[source_index + 1U].body_block;
        } else {
            fallthrough_target = exit_block;
        }
        status = set_branch(context, context->block_id, statement->span, fallthrough_target);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        if (reentry_index != SIZE_MAX) {
            const MinicStatement *reentry_label;
            MinicCoreBlockId reentry_block;
            MinicBlock reentry_segment;
            bool reentry_terminated;

            reentry_label = minic_c0_program_statement(
                context->body->program, body->statements[reentry_index]);
            if (reentry_label == NULL || reentry_label->kind != MINIC_STATEMENT_LABEL ||
                reentry_label->target_expression != MINIC_EXPRESSION_INVALID ||
                reentry_label->expression != MINIC_EXPRESSION_INVALID ||
                reentry_label->target_statement != MINIC_STATEMENT_INVALID) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = ensure_statement_block(
                context, body->statements[reentry_index], &reentry_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            context->block_id = reentry_block;
            reentry_terminated = false;
            reentry_segment = *body;
            reentry_segment.statements = body->statements + reentry_index + 1U;
            reentry_segment.statement_count = segment_end - (reentry_index + 1U);
            reentry_segment.statement_capacity = reentry_segment.statement_count;
            if (reentry_segment.statement_count != 0U) {
                saved_break_target = context->break_target;
                context->break_target = exit_block;
                status = lower_block(context, &reentry_segment, &reentry_terminated);
                context->break_target = saved_break_target;
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            if (!reentry_terminated) {
                MinicCoreBlockId reentry_fallthrough;

                reentry_fallthrough =
                    source_index + 1U < label_count
                        ? labels[source_index + 1U].body_block
                        : exit_block;
                status = set_branch(
                    context, context->block_id, reentry_label->span, reentry_fallthrough);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
        }
    }

    all_segments_terminate = default_label != SIZE_MAX && label_count != 0U;
    if (all_segments_terminate) {
        bool next_path_terminates = false;

        for (source_index = label_count; source_index-- > 0U;) {
            bool path_terminates;

            path_terminates =
                segment_terminates[source_index] ||
                (!segment_breaks[source_index] && source_index + 1U < label_count &&
                 next_path_terminates);
            if (!path_terminates) {
                all_segments_terminate = false;
            }
            next_path_terminates = path_terminates;
        }
    }

    context->block_id = exit_block;
    if (all_segments_terminate) {
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

static bool core_unreachable_statement_has_external_reentry(
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

/* M144_UNREFERENCED_LOOP_LABEL_METADATA_OWNER: parser loop normalization can
   leave an otherwise-empty label at the condition tail even when no source
   continue/goto refers to it.  internal_while_label_pair() gives this label a
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

    loop_matches = 0U;
    for (index = 0U; index < program->statement_count; ++index) {
        const MinicStatement *candidate = minic_c0_program_statement(program, index);
        if (candidate == NULL) {
            return false;
        }
        if (candidate->kind == MINIC_STATEMENT_WHILE &&
            internal_while_label_pair(label, candidate)) {
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

static MinicCoreLowerStatus
lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated) {
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
                if (internal_while_label_pair(statement, unreachable_loop)) {
                    if (core_unreachable_statement_has_external_reentry(
                            context,
                            unreachable_loop,
                            source_block->statements[statement_index])) {
                        return MINIC_CORE_LOWER_UNSUPPORTED;
                    }
                    statement_index += 1U;
                    continue;
                }
            }
            if (statement->kind != MINIC_STATEMENT_LABEL) {
                if (core_unreachable_statement_has_external_reentry(
                        context, statement, MINIC_STATEMENT_INVALID)) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                continue;
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
              internal_while_label_pair(
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
        if (statement->kind == MINIC_STATEMENT_LABEL) {
            const MinicStatement *loop = NULL;
            bool internal_loop_label = false;
            if (statement_index + 1U < source_block->statement_count) {
                MinicStatementId next_statement_id = source_block->statements[statement_index + 1U];
                loop = minic_c0_program_statement(context->body->program, next_statement_id);
                internal_loop_label = internal_while_label_pair(statement, loop);
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
                    status = set_branch(context, context->block_id, statement->span, label_block);
                    if (status != MINIC_CORE_LOWER_OK) return status;
                }
                context->block_id = label_block;
            }
        } else {
            switch (statement->kind) {
            case MINIC_STATEMENT_ASSIGN:
                status = lower_assignment(context, statement);
                break;
            case MINIC_STATEMENT_RECORD_COPY:
            case MINIC_STATEMENT_RECORD_INITIALIZE:
                status = lower_record_copy_statement(context, statement);
                break;
            case MINIC_STATEMENT_EXPRESSION:
                status = lower_expression_statement(context, statement);
                if (status == MINIC_CORE_LOWER_OK &&
                    context->block_id < context->function->block_count &&
                    context->function->blocks[context->block_id].has_terminator) {
                    statement_terminated = true;
                }
                break;
            case MINIC_STATEMENT_INLINE_ASM:
                status = minic_core_lower_inline_asm(context, statement);
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
                status = set_branch(
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
                    status = set_branch(
                        context, context->block_id, statement->span, target_block);
                }
                statement_terminated = status == MINIC_CORE_LOWER_OK;
                break;
            }
            case MINIC_STATEMENT_IF:
                status = lower_if(context, statement, &statement_terminated);
                break;
            case MINIC_STATEMENT_WHILE:
                status = lower_while(
                    context, statement, MINIC_STATEMENT_INVALID, &statement_terminated);
                break;
            case MINIC_STATEMENT_SWITCH:
                status = lower_switch(context, statement, &statement_terminated);
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

MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,
                                               const MinicTargetInfo *target,
                                               MinicCoreFunction *output) {
    const MinicFunction *source_function;
    const MinicBlock *source_block;
    MinicCoreFunction lowered;
    MinicCoreLowerContext context;
    MinicCoreBlockId block_id;
    MinicCoreObjectId *local_objects;
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
    for (local_index = 0U; local_index < source_function->local_count; ++local_index) local_objects[local_index] = MINIC_CORE_OBJECT_INVALID;
    if (body->program->statement_count > SIZE_MAX / sizeof(*statement_blocks)) { free(local_objects); return MINIC_CORE_LOWER_ERROR; }
    statement_blocks = body->program->statement_count == 0U ? NULL : (MinicCoreBlockId *)malloc(body->program->statement_count * sizeof(*statement_blocks));
    if (body->program->statement_count != 0U && statement_blocks == NULL) { free(local_objects); return MINIC_CORE_LOWER_ERROR; }
    for (statement_index = 0U; statement_index < body->program->statement_count; ++statement_index) statement_blocks[statement_index] = MINIC_CORE_BLOCK_INVALID;

    minic_core_function_initialize(&lowered);
    if (!minic_core_function_set_signature(&lowered,
                                           source_function->name,
                                           source_function->name_length,
                                           source_function->return_type,
                                           source_function->parameter_types,
                                           source_function->parameter_count) ||
        !minic_core_function_add_block(&lowered, &block_id)) {
        free(statement_blocks); free(local_objects); minic_core_function_destroy(&lowered); return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&context, 0, sizeof(context));
    context.body = body;
    context.source_function = source_function;
    context.target = target;
    context.function = &lowered;
    context.block_id = block_id;
    context.break_target = MINIC_CORE_BLOCK_INVALID;
    context.local_objects = local_objects;
    context.statement_blocks = statement_blocks;
    context.statement_block_count = body->program->statement_count;
    status = lower_parameter_ingress(&context);
    terminated = false;
    if (status == MINIC_CORE_LOWER_OK) status = lower_block(&context, source_block, &terminated);
    free(statement_blocks); free(local_objects);
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
