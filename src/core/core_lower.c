#include "core/core_lower.h"

#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MinicCoreLowerContext {
    const MinicFunctionBodyView *body;
    const MinicFunction *source_function;
    const MinicTargetInfo *target;
    MinicCoreFunction *function;
    MinicCoreBlockId block_id;
    /* M72_NESTED_BREAK_TARGET: nearest active loop/switch exit for a
       semantic break statement. Kept in lowering context so break nested
       below if/compound blocks still targets the enclosing construct. */
    MinicCoreBlockId break_target;
    MinicCoreObjectId *local_objects;
    /* M64_LOCAL_LABEL_BLOCK_ADDRESS: semantic statement -> Core block map. */
    MinicCoreBlockId *statement_blocks;
    size_t statement_block_count;
} MinicCoreLowerContext;

static MinicCoreLowerStatus lower_expression(MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicCoreValueId *value_id);
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
static MinicCoreLowerStatus append_scalar_bitcast(MinicCoreLowerContext *context,
                                                  MinicSourceSpan span,
                                                  MinicType target_type,
                                                  MinicCoreValueId source_value,
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

static bool core_memory_scalar_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
}

/* A bit-field's C value width is not necessarily the width of the memory
   allocation unit containing it. _Bool is the important case: its semantic
   integer width is one bit, while DataLayout allocates one byte. Keep the
   semantic value type for the expression result and choose an unsigned integer
   type whose object size/target width matches the storage unit used by the
   field layout. Reading storage as unsigned also gives signed bit-fields a
   well-defined logical extraction path before explicit sign extension. */
static bool core_bit_field_storage_type(
    const MinicCoreLowerContext *context,
    MinicType value_type,
    MinicType *storage_type,
    unsigned int *storage_width) {
    MinicType candidates[5];
    size_t candidate_alignment;
    size_t candidate_size;
    size_t candidate_index;
    size_t storage_alignment;
    size_t storage_size;
    unsigned int candidate_width;
    unsigned int value_width;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || storage_type == NULL || storage_width == NULL ||
        !minic_type_is_integer(value_type) ||
        !minic_data_layout_type(minic_default_data_layout(),
                                context->body->program,
                                value_type,
                                &storage_size,
                                &storage_alignment) ||
        storage_size == 0U || storage_size > 8U) {
        return false;
    }
    (void)storage_alignment;

    if (minic_type_is_unsigned_integer(value_type) &&
        minic_target_info_integer_width(
            context->target, context->body->program, value_type, &value_width) &&
        (size_t)value_width == storage_size * 8U) {
        *storage_type = value_type;
        *storage_width = value_width;
        return true;
    }

    candidates[0] = minic_type_unsigned_char();
    candidates[1] = minic_type_unsigned_short();
    candidates[2] = minic_type_unsigned_int();
    candidates[3] = minic_type_unsigned_long();
    candidates[4] = minic_type_unsigned_long_long();
    for (candidate_index = 0U; candidate_index < 5U; ++candidate_index) {
        if (minic_data_layout_type(minic_default_data_layout(),
                                   context->body->program,
                                   candidates[candidate_index],
                                   &candidate_size,
                                   &candidate_alignment) &&
            candidate_size == storage_size &&
            minic_target_info_integer_width(context->target,
                                            context->body->program,
                                            candidates[candidate_index],
                                            &candidate_width) &&
            (size_t)candidate_width == storage_size * 8U) {
            (void)candidate_alignment;
            *storage_type = candidates[candidate_index];
            *storage_width = candidate_width;
            return true;
        }
    }
    return false;
}

/* M74_GLOBAL_RECORD_ADDRESS: an object need not be scalar to have an
   address. Core field-address lowering already consumes pointers to records;
   permit global record objects to enter that path just like arrays. */
static bool core_global_addressable_type(MinicType type) {
    return core_memory_scalar_type(type) || minic_type_is_array(type) ||
           minic_type_is_record(type);
}

static bool core_scalar_expression_value_type(const MinicFunctionBodyView *body,
                                              const MinicExpression *expression,
                                              MinicType *value_type) {
    const MinicExpression *statement_result;

    if (body == NULL || body->program == NULL || expression == NULL || value_type == NULL ||
        !core_memory_scalar_type(expression->type)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return false;
        }
        statement_result = minic_c0_program_expression(
            body->program, expression->value.statement_expression.result);
        return statement_result != NULL &&
               core_scalar_expression_value_type(body, statement_result, value_type);
    }
    /* M97_CONDITIONAL_SCALAR_VALUE_TYPE: GNU C may preserve top-level
       qualifiers on a scalar conditional expression whose arms originate as
       qualified lvalues. Once the conditional is consumed as a scalar value,
       those top-level qualifiers do not belong to the Core SSA/storage type. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        return minic_type_unqualified(expression->type, value_type);
    }
    /* M116_POINTER_ARITH_RVALUE_TYPE: pointer-valued +/- is a transported
       scalar value just like a conditional/conversion result.  The semantic AST
       may retain a top-level qualifier inherited from the source object, but
       every Core consumer (calls, returns, nested arithmetic, stores) must see
       the same unqualified rvalue type.  Keep pointee qualifiers intact. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        minic_type_is_pointer(expression->type) &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        return minic_type_unqualified(expression->type, value_type);
    }
    if (expression->value_category == MINIC_VALUE_LVALUE) {
        return minic_type_unqualified(expression->type, value_type);
    }
    if (expression->value_category != MINIC_VALUE_RVALUE) {
        return false;
    }
    *value_type = expression->type;
    return true;
}

static MinicCoreLowerStatus ensure_statement_block(MinicCoreLowerContext *context, MinicStatementId statement_id, MinicCoreBlockId *block_id) {
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
    if (minic_type_is_array(local->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(
            context->body->program, local->type.array_type_id);
        if (local->is_array || array_type == NULL || array_type->element_count == 0U ||
            array_type->is_zero_length) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_core_function_add_object(
                context->function, local->name_span, local->type, object_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }
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
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_local_object(context, local_id, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
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

    if (context == NULL || context->function == NULL || address_id == NULL ||
        base_id >= context->function->value_count ||
        !minic_type_pointee(context->function->values[base_id].type, &base_pointee) ||
        !minic_type_is_record(base_pointee) || base_pointee.record_id != record_id ||
        record_id == MINIC_RECORD_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_FIELD_ADDRESS;
    instruction.span = span;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.field_address.base = base_id;
    instruction.value.field_address.record_id = record_id;
    instruction.value.field_address.field_index = field_index;
    if (!minic_type_pointer_to(field_type, &instruction.type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, address_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_address(MinicCoreLowerContext *context,
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
        if (!core_global_addressable_type(global->type)) {
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
                                                          minic_default_data_layout(),
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
                                                          minic_default_data_layout(),
                                                          base_value_type,
                                                          &element_size)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            pointer_type = base_value_type;
            subscript_status =
                lower_expression(context, expression->value.subscript.base, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
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
            return subscript_status;
        }
        if (index_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[index_value].type, index_value_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        subscript_status =
            reload_scalar_value(context, base->span, pointer_type, base_object, &base_value);
        if (subscript_status != MINIC_CORE_LOWER_OK) {
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
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &offset_instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
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

static MinicCoreLowerStatus append_scalar_bitcast(MinicCoreLowerContext *context,
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
        return lower_record_value_address(
            context, expression->value.statement_expression.result, address_id);
    }
    return MINIC_CORE_LOWER_UNSUPPORTED;
}

/* M114_RECORD_CONDITIONAL_OBJECT: record values remain address-backed in Core.
   Materialize one private result object and copy exactly the selected arm into
   it. Arms may be ordinary address-backed records, compound literals, direct
   record-returning calls, or nested record conditionals. */
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
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {
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
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||
        target->type.record_id != source->type.record_id ||
        !minic_type_unqualified(target->type, &target_type) ||
        !minic_type_unqualified(source->type, &source_type) ||
        !minic_type_equal(target_type, source_type) || !minic_type_is_record(target_type) ||
        (statement->kind == MINIC_STATEMENT_RECORD_COPY && minic_type_is_const(target->type)) ||
        (!direct_record_call && !record_assignment_value &&
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
            if (argument_expression == NULL ||
                !core_scalar_expression_value_type(
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
        if (argument_index < callee->parameter_count &&
            minic_type_is_record(argument_types[argument_index])) {
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
    MinicCoreLowerStatus status;
    MinicExpressionId callee_value_expression_id;
    MinicType callee_value_type;
    MinicType function_type;
    size_t argument_begin;
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
    /* M124_INDIRECT_FUNCTION_DESIGNATOR: frontend/Sema already accepts both
       pointer-to-function callees and function designators such as `(*fp)`.
       The latter carries function type and its dereference does not perform a
       memory load; its operand is the first-class function-pointer value. Keep
       Core's indirect-call ABI/signature path pointer-valued by normalizing
       only that semantic designator form back to the operand expression. */
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
            (void)fprintf(stderr,
                          "CORE_LOWER_DETAIL marker=M124_INDIRECT_FUNCTION_DESIGNATOR "
                          "function=%s stage=indirect-call reason=dereference-operand-shape\n",
                          context->source_function != NULL ? context->source_function->name : "?");
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    } else if (callee_expression == NULL ||
               !core_scalar_expression_value_type(
                   context->body, callee_expression, &callee_value_type) ||
               !minic_type_pointee(callee_value_type, &function_type) ||
               !minic_type_is_function(function_type)) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=callee-shape callee_kind=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      callee_expression != NULL ? (int)callee_expression->kind : -1);
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    signature = minic_c0_program_function_type(
        context->body->program, function_type.function_type_id);
    if (signature == NULL || signature->is_variadic ||
        expression->value.call.argument_count != signature->parameter_count ||
        !minic_type_equal(expression->type, signature->return_type)) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=signature signature=%d variadic=%d argc=%zu expected=%zu return_match=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      signature != NULL ? 1 : 0,
                      signature != NULL && signature->is_variadic ? 1 : 0,
                      expression->value.call.argument_count,
                      signature != NULL ? signature->parameter_count : 0U,
                      signature != NULL && minic_type_equal(expression->type, signature->return_type) ? 1 : 0);
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    returns_void = minic_type_is_void(signature->return_type);
    if (!returns_void && !core_memory_scalar_type(signature->return_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    for (argument_index = 0U; argument_index < signature->parameter_count; ++argument_index) {
        if (!core_memory_scalar_type(signature->parameter_types[argument_index])) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    }

    /* C leaves the relative evaluation order of the function designator and
       call arguments unspecified.  Lower and stabilize arguments first, then
       evaluate the callee exactly once in the final call block.  This keeps the
       callee SSA value block-local even when an argument creates control flow. */
    arguments = signature->parameter_count == 0U
                    ? NULL
                    : (MinicCoreCallArgument *)calloc(
                          signature->parameter_count, sizeof(*arguments));
    if (signature->parameter_count != 0U && arguments == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (argument_index = 0U; argument_index < signature->parameter_count; ++argument_index) {
        arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
        status = lower_scalar_assignment_value(
            context,
            signature->parameter_types[argument_index],
            expression->value.call.arguments[argument_index],
            &arguments[argument_index].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
        if (arguments[argument_index].value.value_id >= context->function->value_count ||
            !minic_type_equal(
                context->function->values[arguments[argument_index].value.value_id].type,
                signature->parameter_types[argument_index])) {
            free(arguments);
            return MINIC_CORE_LOWER_ERROR;
        }
        status = spill_scalar_value(context,
                                    expression->span,
                                    signature->parameter_types[argument_index],
                                    arguments[argument_index].value.value_id,
                                    &argument_objects[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    /* M112_INDIRECT_CALL_FINAL_BLOCK_ARGUMENTS: argument expressions may
       create control flow, and so may the indirect callee expression (for
       example an address-backed/statement-expression function-pointer load).
       Keep argument values in Core objects until the callee has been evaluated;
       then reload them in the final call block so the verifier sees both the
       callee SSA value and every call argument as block-local available values. */
    status = lower_expression(context, callee_value_expression_id, &callee_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=callee-lower status=%d callee_kind=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status,
                      callee_expression != NULL ? (int)callee_expression->kind : -1);
        free(arguments);
        return status;
    }
    for (argument_index = 0U; argument_index < signature->parameter_count; ++argument_index) {
        status = reload_scalar_value(context,
                                     expression->span,
                                     signature->parameter_types[argument_index],
                                     argument_objects[argument_index],
                                     &arguments[argument_index].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    if (callee_value >= context->function->value_count ||
        !minic_type_equal(context->function->values[callee_value].type, callee_value_type)) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=callee-value-type value=%u count=%zu\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (unsigned int)callee_value, context->function->value_count);
        free(arguments);
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_core_function_add_call_signature(context->function,
                                                function_type.function_type_id,
                                                signature->return_type,
                                                signature->parameter_types,
                                                signature->parameter_count,
                                                &signature_id) ||
        !minic_core_function_append_call_arguments(
            context->function, arguments, signature->parameter_count, &argument_begin)) {
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
    instruction.value.indirect_call.argument_count = signature->parameter_count;
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

static MinicCoreLowerStatus lower_expression(MinicCoreLowerContext *context,
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
                !minic_data_layout_record_field_layout(minic_default_data_layout(),
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
                                                          minic_default_data_layout(),
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

    if (expression->kind == MINIC_EXPRESSION_CALL) {
        if (expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
            return lower_indirect_call(context, expression, value_id);
        }
        return lower_direct_call(context, expression, value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_FIXED_REGISTER) {
        const MinicFixedRegisterBinding *binding;

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
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.fixed_register_binding_id =
            expression->value.fixed_register_binding_id;
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
    if (expression->kind == MINIC_EXPRESSION_CONVERSION) {
        const MinicExpression *operand;
        MinicExpressionId operand_id;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;
        MinicType target_type;

        operand_id = expression->value.unary.operand;
        operand = minic_c0_program_expression(context->body->program, operand_id);
        if (operand == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_type_is_integer(expression->type) || !minic_type_is_integer(operand->type) ||
            !minic_type_unqualified(expression->type, &target_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, operand_id, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        return append_integer_conversion(
            context, expression->span, target_type, operand_value, value_id);
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
        MinicType result_type;

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
            result_pointer_expression == NULL ||
            !minic_type_pointee(result_pointer_expression->type, &result_type) ||
            !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||
            minic_type_is_const(result_type) || minic_type_is_volatile(result_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.overflow.left, &left_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = append_integer_conversion(
            context, left_expression->span, result_type, left_source, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            spill_scalar_value(context, left_expression->span, result_type, left, &left_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.overflow.right, &right_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = append_integer_conversion(
            context, right_expression->span, result_type, right_source, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            spill_scalar_value(context, right_expression->span, result_type, right, &right_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            lower_expression(context, expression->value.overflow.result_pointer, &result_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            reload_scalar_value(context, left_expression->span, result_type, left_object, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            reload_scalar_value(context, right_expression->span, result_type, right_object, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (left >= context->function->value_count || right >= context->function->value_count ||
            result_address >= context->function->value_count ||
            !minic_type_equal(context->function->values[left].type, result_type) ||
            !minic_type_equal(context->function->values[right].type, result_type) ||
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
            if (!minic_c0_pointer_relational_compatible(
                    context->body->program, left_type, right_type) ||
                !minic_type_conditional_pointer_common(left_type, right_type, &common_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
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
                                                      minic_default_data_layout(),
                                                      left_type,
                                                      &element_size)) {
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
                                                      minic_default_data_layout(),
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
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_XOR)) {
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
                !minic_type_is_unsigned_integer(bit_value_type) ||
                !minic_type_is_integer(bit_source->type) || context->target == NULL ||
                !minic_type_unqualified(expression->type, &bit_expression_value_type) ||
                !minic_type_equal(bit_expression_value_type, bit_value_type) ||
                !core_bit_field_storage_type(
                    context, bit_value_type, &bit_storage_type, &bit_storage_width) ||
                bit_storage_width == 0U || bit_storage_width > 64U ||
                bit_field->bit_width > bit_storage_width ||
                !minic_data_layout_record_field_layout(minic_default_data_layout(),
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
                !minic_type_is_unsigned_integer(value_type) ||
                minic_type_is_const(target->type) ||
                !core_bit_field_storage_type(
                    context, value_type, &storage_type, &storage_width) ||
                storage_width == 0U || storage_width > 64U ||
                field->bit_width > storage_width ||
                !minic_data_layout_record_field_layout(minic_default_data_layout(),
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
        (void)fprintf(stderr,
                      "CORE_ASSIGN_STAGE function=%s stage=value status=%d source_kind=%d operand_kind=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status, source_kind, source_operand_kind);
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

/* M102_UNSIGNED_BIT_FIELD_SCALAR_UPDATE: prefix/postfix ++/-- on a
   bit-field cannot use the ordinary addressable-lvalue update path. Evaluate
   the member base once, extract the unsigned field from its storage unit,
   apply the integer promotion and +/-1, convert the result back to the field
   type, then merge it into the original storage unit with one RMW. */
static MinicCoreLowerStatus lower_unsigned_bit_field_update(
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
        !minic_type_is_integer(value_type) || !minic_type_is_unsigned_integer(value_type) ||
        minic_type_is_bool_integer(value_type) ||
        !minic_type_unqualified(expression->type, &expression_value_type) ||
        !minic_type_equal(expression_value_type, value_type) ||
        !minic_target_info_integer_promotion_for_program(
            context->target, context->body->program, value_type, &promoted_type) ||
        !core_bit_field_storage_type(
            context, value_type, &storage_type, &storage_width) ||
        storage_width == 0U || storage_width > 64U || field->bit_width > storage_width ||
        !minic_data_layout_record_field_layout(minic_default_data_layout(),
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
            return lower_unsigned_bit_field_update(
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
                context->body->program, minic_default_data_layout(), stored_type, &element_size)) {
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
            /* BATCH_K_GNU_VOID_RETURN_EXPRESSION: Linux uses GNU's
               `return void_call(...);` extension in thin void wrappers.
               The call still has to be evaluated for effects, after which the
               enclosing function returns normally. Keep this narrow to call
               expressions whose semantic type is already void; all other
               value-bearing return forms remain fail-closed. */
            if (!minic_type_is_void(return_expression->type) ||
                return_expression->kind != MINIC_EXPRESSION_CALL) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_expression(context, statement->expression, &discarded_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
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
        if (minic_type_is_integer(context->source_function->return_type)) {
            /* BATCH_O_SCALAR_RETURN_ASSIGNMENT_CONVERSION: integer return
               contexts use ordinary C assignment conversion too.  Reuse the
               scalar seam so pointer truth values can return as _Bool while
               ordinary integer returns keep the established integer path. */
            status = lower_scalar_assignment_value(context,
                                                   context->source_function->return_type,
                                                   statement->expression,
                                                   &terminator.return_value);
        } else if (minic_type_is_pointer(context->source_function->return_type)) {
            /* M62_POINTER_RETURN_CONVERSION: return uses assignment conversion.
               In particular, T * may return as volatile T * / const T * without
               requiring the source expression to already carry the exact pointer
               qualifiers. Reuse the scalar assignment seam rather than imposing
               an exact-type Core artifact at the return boundary. */
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
    MinicBlock normalized_do_while_body;
    MinicStatementId normalized_do_while_continue;
    MinicCoreBlockId body_block;
    MinicCoreBlockId condition_block;
    MinicCoreBlockId exit_block;
    MinicCoreBlockId preheader_block;
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
    if (normalized_for_update_tail(
            context, statement, body_source, &normalized_for_body, &for_update)) {
        iteration_source = &normalized_for_body;
        normalized_for = true;
    } else if (normalized_for_continue_tail(
                   context, statement, body_source, &normalized_for_body)) {
        iteration_source = &normalized_for_body;
        normalized_for = true;
    }
    if (statement->expression == MINIC_EXPRESSION_INVALID && !normalized_for) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    preheader_block = context->block_id;
    if (!minic_core_function_add_block(context->function, &condition_block) ||
        !minic_core_function_add_block(context->function, &body_block) ||
        !minic_core_function_add_block(context->function, &exit_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (continue_label_statement != MINIC_STATEMENT_INVALID) {
        if (context->statement_blocks == NULL ||
            continue_label_statement >= context->statement_block_count ||
            context->statement_blocks[continue_label_statement] != MINIC_CORE_BLOCK_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* CORE_LOOP_CONTINUE_TARGET_V1: a parser-owned while continue label
           denotes condition re-evaluation. Bind the source label directly to
           the real condition block before lowering the body, so continue does
           not manufacture an orphan block. */
        context->statement_blocks[continue_label_statement] = condition_block;
    }
    status = set_branch(context, preheader_block, statement->span, condition_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = condition_block;
    if (statement->expression == MINIC_EXPRESSION_INVALID) {
        /* C defines an omitted for-condition as true. Keep an explicit Core
           condition block so break/backedge ownership remains identical to
           the conditional-loop path. */
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
    context->block_id = exit_block;
    *terminated = false;
    return MINIC_CORE_LOWER_OK;
}

static bool core_inline_asm_constraint_is(const MinicInlineAsmOperand *operand,
                                              const char *text) {
    size_t length;

    if (operand == NULL || text == NULL || operand->constraint_text == NULL) {
        return false;
    }
    length = strlen(text);
    return operand->constraint_length == length &&
           memcmp(operand->constraint_text, text, length) == 0;
}

static bool core_inline_asm_register_output_constraint(const MinicInlineAsmOperand *operand) {
    return core_inline_asm_constraint_is(operand, "=r") ||
           core_inline_asm_constraint_is(operand, "=&r");
}

/* M105_FIXED_REGISTER_STRUCTURED_ASM: translate the frontend side-table
   reference into a stable Program-owned id without copying target register
   strings into Core. */
static bool core_inline_asm_local_fixed_binding_id(const MinicC0Program *program,
                                                   const MinicExpression *expression,
                                                   size_t *binding_id) {
    const MinicFixedRegisterBinding *binding;
    size_t index;

    if (program == NULL || expression == NULL || binding_id == NULL ||
        expression->kind != MINIC_EXPRESSION_LOCAL) {
        return false;
    }
    binding = minic_c0_program_local_fixed_register_binding(program, expression->value.local_id);
    if (binding == NULL) {
        return false;
    }
    for (index = 0U; index < program->fixed_register_binding_count; ++index) {
        if (&program->fixed_register_bindings[index] == binding) {
            *binding_id = index;
            return true;
        }
    }
    return false;
}

/* M61_IMMEDIATE_ONLY_INLINE_ASM: GNU "i" operands are compile-time
   textual operands. Specialize an all-immediate asm template while Core still
   has access to the semantic program, then transport the resulting target
   text through the existing opaque-asm instruction. This keeps Core unaware
   of RISC-V BUG/WARN semantics and avoids runtime SSA values for constants. */
#define MINIC_CORE_IMMEDIATE_ASM_LIMIT 8U
#define MINIC_CORE_IMMEDIATE_TEXT_LIMIT 64U

static const MinicExpression *core_inline_asm_strip_immediate_wrappers(
    const MinicC0Program *program, MinicExpressionId expression_id) {
    const MinicExpression *expression;

    if (program == NULL) {
        return NULL;
    }
    expression = minic_c0_program_expression(program, expression_id);
    while (expression != NULL &&
           (expression->kind == MINIC_EXPRESSION_CAST ||
            expression->kind == MINIC_EXPRESSION_BITCAST ||
            expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression = minic_c0_program_expression(program, expression->value.unary.operand);
    }
    return expression;
}

static const char *core_inline_asm_symbolic_immediate_name(
    const MinicC0Program *program,
    const MinicTargetInfo *target,
    MinicExpressionId expression_id) {
    const MinicExpression *expression;

    expression = core_inline_asm_strip_immediate_wrappers(program, expression_id);
    if (expression == NULL) {
        return NULL;
    }
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        const MinicFunction *function;

        function = minic_c0_program_function(program, expression->value.function_id);
        if (function == NULL) {
            return NULL;
        }
        if (function->assembler_name != NULL && function->assembler_name_length != 0U) {
            return function->assembler_name;
        }
        return function->name_length == 0U ? NULL : function->name;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(program, expression->value.global_object_id);
        return object == NULL || object->name_length == 0U ? NULL : object->name;
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        const MinicExpression *addressed;

        addressed = core_inline_asm_strip_immediate_wrappers(
            program, expression->value.unary.operand);
        if (addressed == NULL) {
            return NULL;
        }
        if (addressed->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
            const MinicGlobalObject *object;

            object = minic_c0_program_global_object(program, addressed->value.global_object_id);
            return object == NULL || object->name_length == 0U ? NULL : object->name;
        }
        if (addressed->kind == MINIC_EXPRESSION_SUBSCRIPT) {
            const MinicExpression *base;
            MinicConstValue index_value;
            bool index_is_zero;

            base = core_inline_asm_strip_immediate_wrappers(
                program, addressed->value.subscript.base);
            if (base == NULL || base->kind != MINIC_EXPRESSION_GLOBAL_OBJECT || target == NULL ||
                !minic_const_eval_integer(
                    program, target, addressed->value.subscript.index, &index_value) ||
                !minic_const_value_is_zero(
                    program, target, &index_value, &index_is_zero) ||
                !index_is_zero) {
                return NULL;
            }
            {
                const MinicGlobalObject *object;

                object = minic_c0_program_global_object(program, base->value.global_object_id);
                return object == NULL || object->name_length == 0U ? NULL : object->name;
            }
        }
    }
    return NULL;
}

static bool core_inline_asm_immediate_text(
    const MinicCoreLowerContext *context,
    const MinicInlineAsmOperand *operand,
    char *integer_text,
    size_t integer_capacity,
    const char **text_out,
    size_t *length_out) {
    MinicConstValue constant;
    int64_t value;
    const char *symbol;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || operand == NULL || integer_text == NULL ||
        integer_capacity == 0U || text_out == NULL || length_out == NULL ||
        operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
        (!core_inline_asm_constraint_is(operand, "i") &&
         !core_inline_asm_constraint_is(operand, "I"))) {
        return false;
    }
    if (minic_const_eval_integer(
            context->body->program, context->target, operand->expression, &constant) &&
        minic_const_value_as_int64(
            context->body->program, context->target, &constant, &value)) {
        int written;

        written = snprintf(integer_text, integer_capacity, "%" PRId64, value);
        if (written < 0 || (size_t)written >= integer_capacity) {
            return false;
        }
        *text_out = integer_text;
        *length_out = (size_t)written;
        return true;
    }
    symbol = core_inline_asm_symbolic_immediate_name(
        context->body->program, context->target, operand->expression);
    if (symbol == NULL) {
        return false;
    }
    *text_out = symbol;
    *length_out = strlen(symbol);
    return *length_out != 0U;
}

static bool core_inline_asm_named_input_index(const MinicInlineAsm *source,
                                              const char *name,
                                              size_t name_length,
                                              size_t *input_index) {
    size_t index;

    if (source == NULL || source->inputs == NULL || name == NULL || name_length == 0U ||
        input_index == NULL) {
        return false;
    }
    for (index = 0U; index < source->input_count; ++index) {
        const MinicInlineAsmOperand *operand;

        operand = &source->inputs[index];
        if (operand->name != NULL && operand->name_length == name_length &&
            memcmp(operand->name, name, name_length) == 0) {
            *input_index = index;
            return true;
        }
    }
    return false;
}

static bool core_inline_asm_specialized_length(const MinicInlineAsm *source,
                                               const size_t *replacement_lengths,
                                               size_t *specialized_length) {
    size_t cursor;
    size_t output_length;

    if (source == NULL || source->template_text == NULL || replacement_lengths == NULL ||
        specialized_length == NULL) {
        return false;
    }
    cursor = 0U;
    output_length = 0U;
    while (cursor < source->template_length) {
        size_t replacement_index;
        size_t consumed;

        if (source->template_text[cursor] != '%') {
            if (output_length == SIZE_MAX) {
                return false;
            }
            output_length += 1U;
            cursor += 1U;
            continue;
        }
        if (cursor + 1U >= source->template_length) {
            return false;
        }
        if (source->template_text[cursor + 1U] == '%') {
            if (output_length == SIZE_MAX) {
                return false;
            }
            output_length += 1U;
            cursor += 2U;
            continue;
        }
        replacement_index = SIZE_MAX;
        consumed = 0U;
        if (source->template_text[cursor + 1U] >= '0' &&
            source->template_text[cursor + 1U] <= '9') {
            replacement_index = (size_t)(source->template_text[cursor + 1U] - '0');
            consumed = 2U;
        } else if (source->template_text[cursor + 1U] == '[') {
            size_t name_begin;
            size_t name_end;

            name_begin = cursor + 2U;
            name_end = name_begin;
            while (name_end < source->template_length && source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (name_end >= source->template_length || name_end == name_begin ||
                !core_inline_asm_named_input_index(source,
                                                   source->template_text + name_begin,
                                                   name_end - name_begin,
                                                   &replacement_index)) {
                return false;
            }
            consumed = name_end - cursor + 1U;
        } else {
            return false;
        }
        if (replacement_index >= source->input_count ||
            output_length > SIZE_MAX - replacement_lengths[replacement_index]) {
            return false;
        }
        output_length += replacement_lengths[replacement_index];
        cursor += consumed;
    }
    *specialized_length = output_length;
    return true;
}

static bool core_inline_asm_specialize_immediates(const MinicCoreLowerContext *context,
                                                  const MinicInlineAsm *source,
                                                  char **template_out,
                                                  size_t *template_length_out) {
    char integer_text[MINIC_CORE_IMMEDIATE_ASM_LIMIT][MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
    const char *replacements[MINIC_CORE_IMMEDIATE_ASM_LIMIT];
    size_t replacement_lengths[MINIC_CORE_IMMEDIATE_ASM_LIMIT];
    size_t input_index;
    size_t specialized_length;
    size_t cursor;
    size_t output_cursor;
    char *specialized;

    if (context == NULL || source == NULL || template_out == NULL ||
        template_length_out == NULL || source->input_count == 0U ||
        source->input_count > MINIC_CORE_IMMEDIATE_ASM_LIMIT || source->inputs == NULL) {
        return false;
    }
    for (input_index = 0U; input_index < source->input_count; ++input_index) {
        if (!core_inline_asm_immediate_text(context,
                                            &source->inputs[input_index],
                                            integer_text[input_index],
                                            sizeof(integer_text[input_index]),
                                            &replacements[input_index],
                                            &replacement_lengths[input_index])) {
            return false;
        }
    }
    if (!core_inline_asm_specialized_length(source, replacement_lengths, &specialized_length) ||
        specialized_length == SIZE_MAX) {
        return false;
    }
    specialized = (char *)malloc(specialized_length + 1U);
    if (specialized == NULL) {
        return false;
    }
    cursor = 0U;
    output_cursor = 0U;
    while (cursor < source->template_length) {
        size_t replacement_index;
        size_t consumed;

        if (source->template_text[cursor] != '%') {
            specialized[output_cursor++] = source->template_text[cursor++];
            continue;
        }
        if (source->template_text[cursor + 1U] == '%') {
            specialized[output_cursor++] = '%';
            cursor += 2U;
            continue;
        }
        replacement_index = SIZE_MAX;
        consumed = 0U;
        if (source->template_text[cursor + 1U] >= '0' &&
            source->template_text[cursor + 1U] <= '9') {
            replacement_index = (size_t)(source->template_text[cursor + 1U] - '0');
            consumed = 2U;
        } else {
            size_t name_begin;
            size_t name_end;

            name_begin = cursor + 2U;
            name_end = name_begin;
            while (source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (!core_inline_asm_named_input_index(source,
                                                   source->template_text + name_begin,
                                                   name_end - name_begin,
                                                   &replacement_index)) {
                free(specialized);
                return false;
            }
            consumed = name_end - cursor + 1U;
        }
        (void)memcpy(specialized + output_cursor,
                     replacements[replacement_index],
                     replacement_lengths[replacement_index]);
        output_cursor += replacement_lengths[replacement_index];
        cursor += consumed;
    }
    specialized[output_cursor] = '\0';
    if (output_cursor != specialized_length) {
        free(specialized);
        return false;
    }
    *template_out = specialized;
    *template_length_out = specialized_length;
    return true;
}

/* BATCH_I_REGISTER_OUTPUT_IMMEDIATE_SPECIALIZATION: a value-producing
   asm may have one runtime register output plus compile-time-only immediate
   inputs. Preserve %0 for the output and bake %1..%9 into target text using
   the existing i/I constant/symbol resolver. No target instruction meaning is
   introduced into Core. */
static bool core_inline_asm_specialize_register_output_immediates(
    const MinicCoreLowerContext *context,
    const MinicInlineAsm *source,
    char **template_out,
    size_t *template_length_out) {
    char integer_text[MINIC_CORE_IMMEDIATE_ASM_LIMIT][MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
    const char *replacements[MINIC_CORE_IMMEDIATE_ASM_LIMIT];
    size_t replacement_lengths[MINIC_CORE_IMMEDIATE_ASM_LIMIT];
    size_t input_index;
    size_t cursor;
    size_t output_length;
    size_t output_cursor;
    char *specialized;

    if (context == NULL || source == NULL || template_out == NULL ||
        template_length_out == NULL || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 1U ||
        source->input_count == 0U || source->input_count > 9U ||
        source->input_count > MINIC_CORE_IMMEDIATE_ASM_LIMIT || source->inputs == NULL) {
        return false;
    }
    for (input_index = 0U; input_index < source->input_count; ++input_index) {
        if (!core_inline_asm_immediate_text(context,
                                            &source->inputs[input_index],
                                            integer_text[input_index],
                                            sizeof(integer_text[input_index]),
                                            &replacements[input_index],
                                            &replacement_lengths[input_index])) {
            return false;
        }
    }

    cursor = 0U;
    output_length = 0U;
    while (cursor < source->template_length) {
        size_t operand_index;

        if (source->template_text[cursor] != '%') {
            if (output_length == SIZE_MAX) return false;
            output_length += 1U;
            cursor += 1U;
            continue;
        }
        if (cursor + 1U >= source->template_length) return false;
        if (source->template_text[cursor + 1U] == '%') {
            if (output_length == SIZE_MAX) return false;
            output_length += 1U;
            cursor += 2U;
            continue;
        }
        if (source->template_text[cursor + 1U] < '0' ||
            source->template_text[cursor + 1U] > '9') {
            return false;
        }
        operand_index = (size_t)(source->template_text[cursor + 1U] - '0');
        if (operand_index == 0U) {
            if (output_length > SIZE_MAX - 2U) return false;
            output_length += 2U;
        } else {
            input_index = operand_index - 1U;
            if (input_index >= source->input_count ||
                output_length > SIZE_MAX - replacement_lengths[input_index]) {
                return false;
            }
            output_length += replacement_lengths[input_index];
        }
        cursor += 2U;
    }
    if (output_length == SIZE_MAX) return false;
    specialized = (char *)malloc(output_length + 1U);
    if (specialized == NULL) return false;

    cursor = 0U;
    output_cursor = 0U;
    while (cursor < source->template_length) {
        size_t operand_index;

        if (source->template_text[cursor] != '%') {
            specialized[output_cursor++] = source->template_text[cursor++];
            continue;
        }
        if (source->template_text[cursor + 1U] == '%') {
            specialized[output_cursor++] = '%';
            cursor += 2U;
            continue;
        }
        operand_index = (size_t)(source->template_text[cursor + 1U] - '0');
        if (operand_index == 0U) {
            specialized[output_cursor++] = '%';
            specialized[output_cursor++] = '0';
        } else {
            input_index = operand_index - 1U;
            (void)memcpy(specialized + output_cursor,
                         replacements[input_index],
                         replacement_lengths[input_index]);
            output_cursor += replacement_lengths[input_index];
        }
        cursor += 2U;
    }
    specialized[output_cursor] = '\0';
    if (output_cursor != output_length) {
        free(specialized);
        return false;
    }
    *template_out = specialized;
    *template_length_out = output_length;
    return true;
}

/* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: normalize GNU named operand
   references to Core's compact numeric operand indices. Constraint semantics
   stay at the lowering boundary; Core itself only retains operand roles. */
static bool core_inline_asm_named_operand_index(const MinicInlineAsm *source,
                                                const char *name,
                                                size_t name_length,
                                                size_t *operand_index) {
    size_t index;

    if (source == NULL || name == NULL || name_length == 0U || operand_index == NULL) {
        return false;
    }
    for (index = 0U; index < source->output_count; ++index) {
        const MinicInlineAsmOperand *operand = &source->outputs[index];
        if (operand->name != NULL && operand->name_length == name_length &&
            memcmp(operand->name, name, name_length) == 0) {
            *operand_index = index;
            return true;
        }
    }
    for (index = 0U; index < source->input_count; ++index) {
        const MinicInlineAsmOperand *operand = &source->inputs[index];
        if (operand->name != NULL && operand->name_length == name_length &&
            memcmp(operand->name, name, name_length) == 0) {
            *operand_index = source->output_count + index;
            return true;
        }
    }
    return false;
}

/* M69_STRUCTURED_ASM_REGISTER_OR_ZERO: normalize named operands while
   preserving a single GNU operand print modifier (for example RISC-V %z).
   Core does not interpret the modifier; the target backend owns that dialect. */
static bool core_inline_asm_numeric_template(const MinicInlineAsm *source,
                                             char **template_out,
                                             size_t *template_length_out) {
    size_t cursor;
    size_t output_length;
    char *normalized;

    if (source == NULL || template_out == NULL || template_length_out == NULL ||
        source->template_text == NULL || source->template_length == 0U ||
        source->output_count + source->input_count > 10U) {
        return false;
    }
    normalized = (char *)malloc(source->template_length + 1U);
    if (normalized == NULL) {
        return false;
    }
    cursor = 0U;
    output_length = 0U;
    while (cursor < source->template_length) {
        size_t operand_index;
        char modifier;

        if (source->template_text[cursor] != '%') {
            normalized[output_length++] = source->template_text[cursor++];
            continue;
        }
        if (cursor + 1U >= source->template_length) {
            free(normalized);
            return false;
        }
        normalized[output_length++] = '%';
        cursor += 1U;
        if (source->template_text[cursor] == '%') {
            normalized[output_length++] = '%';
            cursor += 1U;
            continue;
        }
        modifier = '\0';
        if ((source->template_text[cursor] >= 'A' && source->template_text[cursor] <= 'Z') ||
            (source->template_text[cursor] >= 'a' && source->template_text[cursor] <= 'z')) {
            modifier = source->template_text[cursor++];
            if (cursor >= source->template_length) {
                free(normalized);
                return false;
            }
        }
        if (source->template_text[cursor] >= '0' && source->template_text[cursor] <= '9') {
            operand_index = (size_t)(source->template_text[cursor] - '0');
            if (operand_index >= source->output_count + source->input_count) {
                free(normalized);
                return false;
            }
            if (modifier != '\0') {
                normalized[output_length++] = modifier;
            }
            normalized[output_length++] = source->template_text[cursor++];
            continue;
        }
        if (source->template_text[cursor] == '[') {
            size_t name_begin = cursor + 1U;
            size_t name_end = name_begin;
            while (name_end < source->template_length && source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (name_end >= source->template_length || name_end == name_begin ||
                !core_inline_asm_named_operand_index(source,
                                                     source->template_text + name_begin,
                                                     name_end - name_begin,
                                                     &operand_index) ||
                operand_index > 9U) {
                free(normalized);
                return false;
            }
            if (modifier != '\0') {
                normalized[output_length++] = modifier;
            }
            normalized[output_length++] = (char)('0' + operand_index);
            cursor = name_end + 1U;
            continue;
        }
        free(normalized);
        return false;
    }
    normalized[output_length] = '\0';
    *template_out = normalized;
    *template_length_out = output_length;
    return true;
}

/* M76_SINGLE_LABEL_ASM_GOTO: admit the common GNU asm-goto seam without
   teaching Core any Linux/static-key meaning. Keep the initial contract narrow:
   one label, one read-only "i" operand whose value requires the existing
   deferred-immediate mechanism, no outputs/clobbers, and only %0/%l[label]/%%
   template references. */
static bool core_inline_asm_single_label_goto_supported(
    const MinicCoreLowerContext *context, const MinicInlineAsm *source) {
    const MinicExpression *input_expression;
    const MinicInlineAsmLabel *label;
    const MinicStatement *target_statement;
    char immediate_text[MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
    const char *resolved_text;
    size_t resolved_length;
    size_t cursor;
    bool saw_input;
    bool saw_label;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        source == NULL || !source->is_goto || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 0U ||
        source->input_count != 1U || source->inputs == NULL || source->label_count != 1U ||
        source->labels == NULL || source->register_clobber_count != 0U ||
        source->clobber_count != 0U || source->has_memory_clobber) {
        return false;
    }
    if (source->inputs[0].access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
        !core_inline_asm_constraint_is(&source->inputs[0], "i")) {
        return false;
    }
    input_expression =
        minic_c0_program_expression(context->body->program, source->inputs[0].expression);
    if (input_expression == NULL ||
        (!minic_type_is_integer(input_expression->type) &&
         !minic_type_is_pointer(input_expression->type))) {
        return false;
    }
    /* Resolved immediates already have the M61 path. M76 is deliberately the
       deferred-immediate asm-goto seam exposed by always-inline helpers. */
    if (core_inline_asm_immediate_text(context,
                                      &source->inputs[0],
                                      immediate_text,
                                      sizeof(immediate_text),
                                      &resolved_text,
                                      &resolved_length)) {
        return false;
    }
    label = &source->labels[0];
    if (label->name == NULL || label->name_length == 0U ||
        label->target_statement == MINIC_STATEMENT_INVALID) {
        return false;
    }
    target_statement =
        minic_c0_program_statement(context->body->program, label->target_statement);
    if (target_statement == NULL || target_statement->kind != MINIC_STATEMENT_LABEL) {
        return false;
    }

    cursor = 0U;
    saw_input = false;
    saw_label = false;
    while (cursor < source->template_length) {
        if (source->template_text[cursor] != '%') {
            cursor += 1U;
            continue;
        }
        if (cursor + 1U >= source->template_length) {
            return false;
        }
        if (source->template_text[cursor + 1U] == '%') {
            cursor += 2U;
            continue;
        }
        if (source->template_text[cursor + 1U] == '0') {
            saw_input = true;
            cursor += 2U;
            continue;
        }
        if (source->template_text[cursor + 1U] == '[') {
            const MinicInlineAsmOperand *input = &source->inputs[0];
            size_t name_begin = cursor + 2U;
            size_t name_end = name_begin;

            while (name_end < source->template_length &&
                   source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (input->name == NULL || input->name_length == 0U ||
                name_end >= source->template_length || name_end == name_begin ||
                name_end - name_begin != input->name_length ||
                memcmp(source->template_text + name_begin, input->name, input->name_length) != 0) {
                return false;
            }
            saw_input = true;
            cursor = name_end + 1U;
            continue;
        }
        if (cursor + 3U < source->template_length &&
            source->template_text[cursor + 1U] == 'l' &&
            source->template_text[cursor + 2U] == '[') {
            size_t name_begin = cursor + 3U;
            size_t name_end = name_begin;
            while (name_end < source->template_length &&
                   source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (name_end >= source->template_length || name_end == name_begin ||
                name_end - name_begin != label->name_length ||
                memcmp(source->template_text + name_begin, label->name, label->name_length) != 0) {
                return false;
            }
            saw_label = true;
            cursor = name_end + 1U;
            continue;
        }
        return false;
    }
    return saw_input && saw_label;
}

static bool core_inline_asm_single_label_goto_numeric_template(
    const MinicInlineAsm *source, char **template_out, size_t *template_length_out) {
    char *normalized;
    size_t cursor;
    size_t output_length;

    if (source == NULL || template_out == NULL || template_length_out == NULL ||
        source->template_text == NULL || source->inputs == NULL || source->input_count != 1U) {
        return false;
    }
    normalized = (char *)malloc(source->template_length + 1U);
    if (normalized == NULL) {
        return false;
    }
    cursor = 0U;
    output_length = 0U;
    while (cursor < source->template_length) {
        if (source->template_text[cursor] == '%' && cursor + 1U < source->template_length &&
            source->template_text[cursor + 1U] == '[') {
            const MinicInlineAsmOperand *input = &source->inputs[0];
            size_t name_begin = cursor + 2U;
            size_t name_end = name_begin;

            while (name_end < source->template_length &&
                   source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (input->name == NULL || input->name_length == 0U ||
                name_end >= source->template_length || name_end == name_begin ||
                name_end - name_begin != input->name_length ||
                memcmp(source->template_text + name_begin, input->name, input->name_length) != 0) {
                free(normalized);
                return false;
            }
            normalized[output_length++] = '%';
            normalized[output_length++] = '0';
            cursor = name_end + 1U;
            continue;
        }
        normalized[output_length++] = source->template_text[cursor++];
    }
    normalized[output_length] = '\0';
    *template_out = normalized;
    *template_length_out = output_length;
    return true;
}

static MinicCoreLowerStatus lower_opaque_inline_asm(MinicCoreLowerContext *context,
                                                    const MinicStatement *statement) {
    const MinicInlineAsm *source;
    MinicCoreInlineAsmId inline_asm_id;
    MinicCoreInstruction instruction;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL ||
        statement->inline_asm_id == MINIC_INLINE_ASM_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source = minic_c0_program_inline_asm(context->body->program, statement->inline_asm_id);
    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    /* M126A_GENERIC_STRUCTURED_ASM: canonical role lowering for register/memory
       extended asm. Preflight is deliberately side-effect free: an asm that
       ultimately belongs to an older/specialized path must not leave partial
       Core values, objects, or instructions behind. Only after every operand
       role and the numeric template are proven do we materialize operands. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->label_count == 0U &&
        source->output_count <= MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT &&
        source->input_count <= MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT - source->output_count &&
        source->output_count + source->input_count != 0U &&
        (source->output_count == 0U || source->outputs != NULL) &&
        (source->input_count == 0U || source->inputs != NULL) &&
        source->clobber_count == source->register_clobber_count +
                                     (source->has_memory_clobber ? 1U : 0U)) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t output_index;
        size_t input_index;
        bool supported_shape = true;

        (void)memset(&structured, 0, sizeof(structured));
        structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
        structured.span = statement->span;
        structured.type = minic_type_void();
        structured.result = MINIC_CORE_VALUE_INVALID;
        structured.value.structured_inline_asm.operand_count =
            source->output_count + source->input_count;

        /* Phase 1: pure classification only. No Core mutation is permitted. */
        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicCoreStructuredInlineAsmOperand *binding =
                &structured.value.structured_inline_asm.operands[output_index];
            MinicType value_type;
            size_t fixed_binding_id;

            if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(expression->type) ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
                break;
            }
            binding->operand_index = output_index;
            binding->early_clobber =
                operand->constraint_text != NULL &&
                memchr(operand->constraint_text, '&', operand->constraint_length) != NULL;
            if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                (core_inline_asm_constraint_is(operand, "=r") ||
                 core_inline_asm_constraint_is(operand, "=&r"))) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       (core_inline_asm_constraint_is(operand, "+r") ||
                        core_inline_asm_constraint_is(operand, "+&r"))) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_constraint_is(operand, "=m")) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
                binding->early_clobber = false;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       (core_inline_asm_constraint_is(operand, "+m") ||
                        core_inline_asm_constraint_is(operand, "+A"))) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
                binding->early_clobber = false;
            } else {
                supported_shape = false;
                break;
            }
            if ((binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                 binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) &&
                core_inline_asm_local_fixed_binding_id(
                    context->body->program, expression, &fixed_binding_id)) {
                binding->fixed_register_binding_id = fixed_binding_id;
                binding->has_fixed_register_binding = true;
            }
        }

        for (input_index = 0U; supported_shape && input_index < source->input_count;
             ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            size_t operand_index = source->output_count + input_index;
            MinicCoreStructuredInlineAsmOperand *binding =
                &structured.value.structured_inline_asm.operands[operand_index];
            MinicType value_type;
            size_t fixed_binding_id;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY || expression == NULL) {
                supported_shape = false;
                break;
            }
            binding->operand_index = operand_index;
            if (core_inline_asm_constraint_is(operand, "m")) {
                if (expression->value_category != MINIC_VALUE_LVALUE ||
                    !minic_type_unqualified(expression->type, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
            } else if (core_inline_asm_constraint_is(operand, "r") ||
                       core_inline_asm_constraint_is(operand, "rJ") ||
                       core_inline_asm_constraint_is(operand, "Jr") ||
                       core_inline_asm_constraint_is(operand, "rK")) {
                if (!core_scalar_expression_value_type(context->body, expression, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                if (core_inline_asm_local_fixed_binding_id(
                        context->body->program, expression, &fixed_binding_id)) {
                    binding->fixed_register_binding_id = fixed_binding_id;
                    binding->has_fixed_register_binding = true;
                }
            } else {
                supported_shape = false;
                break;
            }
        }

        /* Template normalization is also part of preflight. A failed probe
           falls through with the Core function exactly unchanged. */
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            MinicCoreLowerStatus status;
            size_t clobber_index;

            /* Phase 2: commit operand materialization. Any failure from here
               aborts this function lowering, so partial state is destroyed by
               minic_core_lower_function rather than leaking into another path. */
            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                status = lower_address(
                    context, source->outputs[output_index].expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    free(numeric_template);
                    return status;
                }
            }
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                size_t operand_index = source->output_count + input_index;
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[operand_index];
                if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) {
                    status = lower_address(
                        context, source->inputs[input_index].expression, &binding->value);
                } else {
                    status = lower_expression(
                        context, source->inputs[input_index].expression, &binding->value);
                }
                if (status != MINIC_CORE_LOWER_OK) {
                    free(numeric_template);
                    return status;
                }
            }

            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                            numeric_template,
                                                            numeric_template_length,
                                                            true,
                                                            source->has_memory_clobber,
                                                            &inline_asm_id)) {
                free(numeric_template);
                return MINIC_CORE_LOWER_ERROR;
            }
            free(numeric_template);
            numeric_template = NULL;
            for (clobber_index = 0U; clobber_index < source->register_clobber_count;
                 ++clobber_index) {
                const MinicInlineAsmRegisterClobber *clobber =
                    &source->register_clobbers[clobber_index];
                if (clobber->name == NULL || clobber->name_length == 0U ||
                    !minic_core_function_add_inline_asm_register_clobber(
                        context->function,
                        inline_asm_id,
                        clobber->name,
                        clobber->name_length)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    if (core_inline_asm_single_label_goto_supported(context, source)) {
        char *numeric_template;
        size_t numeric_template_length;
        MinicCoreBlockId target_block;
        MinicCoreInlineAsm *stored;
        MinicCoreLowerStatus status;

        status = ensure_statement_block(context, source->labels[0].target_statement, &target_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        numeric_template = NULL;
        numeric_template_length = 0U;
        if (!core_inline_asm_single_label_goto_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                       numeric_template,
                                                       numeric_template_length,
                                                       true,
                                                       false,
                                                       &inline_asm_id)) {
            free(numeric_template);
            return MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
        stored = &context->function->inline_asms[inline_asm_id];
        stored->is_goto = true;
        stored->source_inline_asm_id = (size_t)statement->inline_asm_id;
        stored->goto_target = target_block;

        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
        instruction.span = statement->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.inline_asm_id = inline_asm_id;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

    /* M105_FIXED_REGISTER_STRUCTURED_ASM: Linux SBI-style extended asm uses
       two +r outputs and six r inputs, all backed by GNU local fixed-register
       variables. Preserve the Program-owned binding id on each Core operand;
       the RV64 backend alone interprets names such as a0..a7. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 2U && source->input_count == 6U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t fixed_binding_ids[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
        size_t output_index;
        size_t input_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            const MinicLocal *local;
            MinicType value_type;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_WRITE ||
                (!core_inline_asm_constraint_is(operand, "+r") &&
                 !core_inline_asm_constraint_is(operand, "+&r")) ||
                expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
                expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(expression->type) || minic_type_is_volatile(expression->type) ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !core_memory_scalar_type(value_type) ||
                !core_inline_asm_local_fixed_binding_id(
                    context->body->program, expression, &fixed_binding_ids[output_index])) {
                supported_shape = false;
                break;
            }
            local = minic_c0_program_local(context->body->program, expression->value.local_id);
            if (local == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (local->is_array || !minic_type_equal(local->type, expression->type)) {
                supported_shape = false;
                break;
            }
        }
        for (input_index = 0U; supported_shape && input_index < source->input_count; ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;
            size_t operand_index = source->output_count + input_index;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                !core_inline_asm_constraint_is(operand, "r") || expression == NULL ||
                expression->kind != MINIC_EXPRESSION_LOCAL ||
                !core_scalar_expression_value_type(context->body, expression, &value_type) ||
                !core_memory_scalar_type(value_type) ||
                !core_inline_asm_local_fixed_binding_id(
                    context->body->program, expression, &fixed_binding_ids[operand_index])) {
                supported_shape = false;
            }
        }
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added;

            added = minic_core_function_add_opaque_inline_asm(context->function,
                                                               numeric_template,
                                                               numeric_template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count =
                source->output_count + source->input_count;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                const MinicInlineAsmOperand *operand = &source->outputs[output_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                MinicCoreLowerStatus status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
                binding->operand_index = output_index;
                binding->fixed_register_binding_id = fixed_binding_ids[output_index];
                binding->has_fixed_register_binding = true;
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                const MinicInlineAsmOperand *operand = &source->inputs[input_index];
                size_t operand_index = source->output_count + input_index;
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[operand_index];
                MinicCoreLowerStatus status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = operand_index;
                binding->fixed_register_binding_id = fixed_binding_ids[operand_index];
                binding->has_fixed_register_binding = true;
                status = lower_expression(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    /* M110_PURE_REGISTER_OUTPUT_ASM: ordinary volatile extended asm
       with 1..5 write-only register outputs and no inputs/clobbers. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL &&
        source->output_count >= 1U && source->output_count <= 5U &&
        source->input_count == 0U && source->label_count == 0U &&
        source->register_clobber_count == 0U && !source->has_memory_clobber &&
        source->clobber_count == 0U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t output_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *output_expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY ||
                !core_inline_asm_register_output_constraint(operand) ||
                output_expression == NULL ||
                output_expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(output_expression->type) ||
                !minic_type_unqualified(output_expression->type, &value_type) ||
                !core_memory_scalar_type(value_type) ||
                (output_expression->kind == MINIC_EXPRESSION_LOCAL &&
                 minic_c0_program_local_fixed_register_binding(
                     context->body->program, output_expression->value.local_id) != NULL)) {
                supported_shape = false;
                break;
            }
        }
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added = minic_core_function_add_opaque_inline_asm(context->function,
                                                                    numeric_template,
                                                                    numeric_template_length,
                                                                    true,
                                                                    false,
                                                                    &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = source->output_count;
            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                MinicCoreLowerStatus output_status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                binding->operand_index = output_index;
                output_status = lower_address(
                    context, source->outputs[output_index].expression, &binding->value);
                if (output_status != MINIC_CORE_LOWER_OK) {
                    return output_status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    /* M111_PURE_REGISTER_INPUT_ASM: 1..4 read-only scalar register inputs,
       no outputs/clobbers. This is the input-side dual of M110. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U &&
        source->inputs != NULL && source->input_count >= 1U && source->input_count <= 4U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber && source->clobber_count == 0U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t input_index;
        bool supported_shape = true;

        for (input_index = 0U; input_index < source->input_count; ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *input_expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                !core_inline_asm_constraint_is(operand, "r") ||
                input_expression == NULL ||
                !core_scalar_expression_value_type(context->body, input_expression, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
                break;
            }
        }
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added = minic_core_function_add_opaque_inline_asm(context->function,
                                                                    numeric_template,
                                                                    numeric_template_length,
                                                                    true,
                                                                    false,
                                                                    &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = source->input_count;
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[input_index];
                MinicCoreLowerStatus input_status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = input_index;
                input_status = lower_expression(
                    context, source->inputs[input_index].expression, &binding->value);
                if (input_status != MINIC_CORE_LOWER_OK) {
                    return input_status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    /* M68_STRUCTURED_INLINE_ASM_OPTIONAL_INPUTS: M67's structured
       operand model is variable-sized. Admit the same proven output/memory
       shape with 0..2 scalar register inputs instead of hard-coding two. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL &&
        (source->input_count == 0U || source->inputs != NULL) &&
        source->output_count == 3U && source->input_count <= 2U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t output_index;
        size_t input_index;
        size_t register_output_count = 0U;
        size_t memory_output_count = 0U;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                core_inline_asm_register_output_constraint(operand)) {
                const MinicLocal *local;
                if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
                    expression->value_category != MINIC_VALUE_LVALUE ||
                    minic_type_is_const(expression->type) || minic_type_is_volatile(expression->type) ||
                    !minic_type_unqualified(expression->type, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                local = minic_c0_program_local(context->body->program, expression->value.local_id);
                if (local == NULL) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (local->is_array ||
                    minic_c0_program_local_fixed_register_binding(
                        context->body->program, expression->value.local_id) != NULL ||
                    !minic_type_equal(local->type, expression->type)) {
                    supported_shape = false;
                    break;
                }
                register_output_count += 1U;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       core_inline_asm_constraint_is(operand, "+A")) {
                if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
                    minic_type_is_const(expression->type) ||
                    !minic_type_unqualified(expression->type, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                memory_output_count += 1U;
            } else {
                supported_shape = false;
                break;
            }
        }
        for (input_index = 0U; supported_shape && input_index < source->input_count; ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;
            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                (!core_inline_asm_constraint_is(operand, "r") &&
                 !core_inline_asm_constraint_is(operand, "rJ")) || expression == NULL ||
                !core_scalar_expression_value_type(context->body, expression, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
            }
        }
        if (supported_shape && register_output_count == 2U && memory_output_count == 1U &&
            core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added;

            added = minic_core_function_add_opaque_inline_asm(context->function,
                                                               numeric_template,
                                                               numeric_template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id);
            free(numeric_template);
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count =
                source->output_count + source->input_count;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                const MinicInlineAsmOperand *operand = &source->outputs[output_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                MinicCoreLowerStatus status;

                binding->operand_index = output_index;
                binding->kind = operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
                                    : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                const MinicInlineAsmOperand *operand = &source->inputs[input_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[source->output_count + input_index];
                MinicCoreLowerStatus status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = source->output_count + input_index;
                status = lower_expression(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U &&
        source->input_count != 0U && source->inputs != NULL && source->label_count == 0U &&
        source->register_clobber_count == 0U && !source->has_memory_clobber &&
        source->clobber_count == 0U) {
        char *specialized_template;
        size_t specialized_length;

        specialized_template = NULL;
        specialized_length = 0U;
        if (core_inline_asm_specialize_immediates(
                context, source, &specialized_template, &specialized_length)) {
            bool added;

            added = specialized_length != 0U &&
                    minic_core_function_add_opaque_inline_asm(context->function,
                                                              specialized_template,
                                                              specialized_length,
                                                              true,
                                                              false,
                                                              &inline_asm_id);
            free(specialized_template);
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.inline_asm_id = inline_asm_id;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->output_count == 0U &&
        source->input_count == 0U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->has_memory_clobber &&
        source->clobber_count == 1U) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
        instruction.span = statement->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

    /* M113_MIXED_ATOMIC_STRUCTURED_ASM: preserve a four-operand
       volatile extended-asm shape consisting of one +r register, one =r/=&r
       register, one +m memory lvalue, and one r/Jr/rJ scalar input with a
       compiler memory clobber. The operand-role model is already generic; this
       only admits the previously unlisted combination. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 3U && source->input_count == 1U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        const MinicInlineAsmOperand *input = &source->inputs[0];
        const MinicExpression *input_expression;
        MinicCoreInstruction structured;
        MinicType input_type;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t register_readwrite_index = SIZE_MAX;
        size_t register_output_index = SIZE_MAX;
        size_t memory_readwrite_index = SIZE_MAX;
        size_t output_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(expression->type) ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
                break;
            }
            if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(operand, "+r")) {
                if (register_readwrite_index != SIZE_MAX ||
                    (expression->kind == MINIC_EXPRESSION_LOCAL &&
                     minic_c0_program_local_fixed_register_binding(
                         context->body->program, expression->value.local_id) != NULL)) {
                    supported_shape = false;
                    break;
                }
                register_readwrite_index = output_index;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_register_output_constraint(operand)) {
                if (register_output_index != SIZE_MAX ||
                    (expression->kind == MINIC_EXPRESSION_LOCAL &&
                     minic_c0_program_local_fixed_register_binding(
                         context->body->program, expression->value.local_id) != NULL)) {
                    supported_shape = false;
                    break;
                }
                register_output_index = output_index;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       core_inline_asm_constraint_is(operand, "+m")) {
                if (memory_readwrite_index != SIZE_MAX) {
                    supported_shape = false;
                    break;
                }
                memory_readwrite_index = output_index;
            } else {
                supported_shape = false;
                break;
            }
        }
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        if (!supported_shape || register_readwrite_index == SIZE_MAX ||
            register_output_index == SIZE_MAX || memory_readwrite_index == SIZE_MAX ||
            input->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            (!core_inline_asm_constraint_is(input, "Jr") &&
             !core_inline_asm_constraint_is(input, "rJ") &&
             !core_inline_asm_constraint_is(input, "r")) ||
            input_expression == NULL ||
            !core_scalar_expression_value_type(context->body, input_expression, &input_type) ||
            !core_memory_scalar_type(input_type)) {
            supported_shape = false;
        }
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            MinicCoreLowerStatus status;
            bool added = minic_core_function_add_opaque_inline_asm(context->function,
                                                                    numeric_template,
                                                                    numeric_template_length,
                                                                    true,
                                                                    true,
                                                                    &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 4U;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                binding->operand_index = output_index;
                binding->kind = output_index == register_readwrite_index
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                                : output_index == register_output_index
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                                    : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
                status = lower_address(
                    context, source->outputs[output_index].expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            structured.value.structured_inline_asm.operands[3].kind =
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            structured.value.structured_inline_asm.operands[3].operand_index = 3U;
            status = lower_expression(
                context, input->expression, &structured.value.structured_inline_asm.operands[3].value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    /* M118_SIX_OPERAND_ATOMIC_STRUCTURED_ASM: preserve a six-operand
       volatile extended-asm shape consisting of one +r register, two =r/=&r
       registers, one +m memory lvalue, and two r/Jr/rJ scalar inputs with a
       compiler memory clobber. Core preserves operand roles; target register
       assignment remains backend-owned. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 4U && source->input_count == 2U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t register_readwrites = 0U;
        size_t register_outputs = 0U;
        size_t memory_readwrites = 0U;
        size_t output_index;
        size_t input_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *output_expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (output_expression == NULL ||
                output_expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(output_expression->type) ||
                !minic_type_unqualified(output_expression->type, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
                break;
            }
            if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(operand, "+r")) {
                if (output_expression->kind == MINIC_EXPRESSION_LOCAL &&
                    minic_c0_program_local_fixed_register_binding(
                        context->body->program, output_expression->value.local_id) != NULL) {
                    supported_shape = false;
                    break;
                }
                register_readwrites += 1U;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_register_output_constraint(operand)) {
                if (output_expression->kind == MINIC_EXPRESSION_LOCAL &&
                    minic_c0_program_local_fixed_register_binding(
                        context->body->program, output_expression->value.local_id) != NULL) {
                    supported_shape = false;
                    break;
                }
                register_outputs += 1U;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       core_inline_asm_constraint_is(operand, "+m")) {
                memory_readwrites += 1U;
            } else {
                supported_shape = false;
                break;
            }
        }
        for (input_index = 0U; supported_shape && input_index < source->input_count;
             ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *input_expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                (!core_inline_asm_constraint_is(operand, "Jr") &&
                 !core_inline_asm_constraint_is(operand, "rJ") &&
                 !core_inline_asm_constraint_is(operand, "r")) ||
                input_expression == NULL ||
                !core_scalar_expression_value_type(
                    context->body, input_expression, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
            }
        }
        if (supported_shape && register_readwrites == 1U && register_outputs == 2U &&
            memory_readwrites == 1U &&
            core_inline_asm_numeric_template(source, &numeric_template, &numeric_template_length)) {
            MinicCoreLowerStatus status;
            bool added = minic_core_function_add_opaque_inline_asm(context->function,
                                                                    numeric_template,
                                                                    numeric_template_length,
                                                                    true,
                                                                    true,
                                                                    &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 6U;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                const MinicInlineAsmOperand *operand = &source->outputs[output_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];

                binding->operand_index = output_index;
                if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                    core_inline_asm_constraint_is(operand, "+r")) {
                    binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
                } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                           core_inline_asm_register_output_constraint(operand)) {
                    binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                } else {
                    binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
                }
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[4U + input_index];
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = 4U + input_index;
                status = lower_expression(
                    context, source->inputs[input_index].expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    /* M125_STRUCTURED_MEMORY_INPUT_ASM: one register read/write output,
       one write-only register output, and one read-only memory input. `m` is
       address-backed in Core; the backend materializes only its address and
       never writes the referenced object after the asm. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 2U && source->input_count == 1U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber && source->clobber_count == 0U) {
        const MinicInlineAsmOperand *input = &source->inputs[0];
        const MinicExpression *input_expression =
            minic_c0_program_expression(context->body->program, input->expression);
        MinicCoreInstruction structured;
        MinicType input_type;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t output_index;
        size_t register_output_count = 0U;
        size_t register_readwrite_count = 0U;
        bool supported_shape = true;

        if (input->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            !core_inline_asm_constraint_is(input, "m") || input_expression == NULL ||
            input_expression->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_unqualified(input_expression->type, &input_type) ||
            !core_memory_scalar_type(input_type)) {
            supported_shape = false;
        }
        for (output_index = 0U; supported_shape && output_index < source->output_count;
             ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(expression->type) ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !core_memory_scalar_type(value_type) ||
                (expression->kind == MINIC_EXPRESSION_LOCAL &&
                 minic_c0_program_local_fixed_register_binding(
                     context->body->program, expression->value.local_id) != NULL)) {
                supported_shape = false;
                break;
            }
            if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(operand, "+r")) {
                register_readwrite_count += 1U;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_register_output_constraint(operand)) {
                register_output_count += 1U;
            } else {
                supported_shape = false;
            }
        }
        if (supported_shape && register_readwrite_count == 1U && register_output_count == 1U &&
            core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            MinicCoreLowerStatus status;
            bool added;

            added = minic_core_function_add_opaque_inline_asm(context->function,
                                                               numeric_template,
                                                               numeric_template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 3U;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                const MinicInlineAsmOperand *operand = &source->outputs[output_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];

                binding->operand_index = output_index;
                binding->kind = operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                                    : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            structured.value.structured_inline_asm.operands[2].kind =
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
            structured.value.structured_inline_asm.operands[2].operand_index = 2U;
            status = lower_address(
                context, input->expression, &structured.value.structured_inline_asm.operands[2].value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    /* M107_STRUCTURED_MEMORY_OUTPUT_ASM: GCC-style asm may pair one
       register read/write output with one write-only memory output and a
       scalar register/immediate input. Preserve those access roles in Core;
       target register allocation and template interpretation remain backend-owned. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 2U && source->input_count == 1U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber && source->clobber_count == 0U) {
        const MinicInlineAsmOperand *input = &source->inputs[0];
        const MinicInlineAsmOperand *memory_output = NULL;
        const MinicInlineAsmOperand *register_output = NULL;
        const MinicExpression *input_expression;
        const MinicExpression *memory_expression;
        const MinicExpression *register_expression;
        const MinicLocal *register_local;
        MinicCoreInstruction structured;
        MinicType input_type;
        MinicType memory_type;
        MinicType register_type;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t memory_index = SIZE_MAX;
        size_t register_index = SIZE_MAX;
        size_t output_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *candidate = &source->outputs[output_index];

            if (candidate->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(candidate, "+r")) {
                if (register_output != NULL) {
                    supported_shape = false;
                    break;
                }
                register_output = candidate;
                register_index = output_index;
            } else if (candidate->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_constraint_is(candidate, "=m")) {
                if (memory_output != NULL) {
                    supported_shape = false;
                    break;
                }
                memory_output = candidate;
                memory_index = output_index;
            } else {
                supported_shape = false;
                break;
            }
        }

        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        memory_expression = memory_output == NULL
                                ? NULL
                                : minic_c0_program_expression(context->body->program,
                                                              memory_output->expression);
        register_expression = register_output == NULL
                                  ? NULL
                                  : minic_c0_program_expression(context->body->program,
                                                                register_output->expression);
        register_local = register_expression == NULL ||
                                 register_expression->kind != MINIC_EXPRESSION_LOCAL
                             ? NULL
                             : minic_c0_program_local(context->body->program,
                                                      register_expression->value.local_id);
        if (!supported_shape || register_output == NULL || memory_output == NULL ||
            register_index == SIZE_MAX || memory_index == SIZE_MAX || input_expression == NULL ||
            memory_expression == NULL || register_expression == NULL || register_local == NULL ||
            input->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            (!core_inline_asm_constraint_is(input, "rJ") &&
             !core_inline_asm_constraint_is(input, "r")) ||
            register_expression->value_category != MINIC_VALUE_LVALUE ||
            memory_expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(register_expression->type) ||
            minic_type_is_volatile(register_expression->type) ||
            minic_type_is_const(memory_expression->type) || register_local->is_array ||
            minic_c0_program_local_fixed_register_binding(
                context->body->program, register_expression->value.local_id) != NULL ||
            !minic_type_equal(register_local->type, register_expression->type) ||
            !minic_type_unqualified(register_expression->type, &register_type) ||
            !minic_type_unqualified(memory_expression->type, &memory_type) ||
            !core_memory_scalar_type(register_type) || !core_memory_scalar_type(memory_type) ||
            !core_scalar_expression_value_type(context->body, input_expression, &input_type) ||
            !core_memory_scalar_type(input_type)) {
            supported_shape = false;
        }

        if (supported_shape &&
            core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            MinicCoreLowerStatus status;

            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                           numeric_template,
                                                           numeric_template_length,
                                                           source->is_volatile,
                                                           source->has_memory_clobber,
                                                           &inline_asm_id)) {
                free(numeric_template);
                return MINIC_CORE_LOWER_ERROR;
            }
            free(numeric_template);
            numeric_template = NULL;
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 3U;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                const MinicInlineAsmOperand *operand = &source->outputs[output_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];

                binding->operand_index = output_index;
                binding->kind = output_index == register_index
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                                    : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            structured.value.structured_inline_asm.operands[2].kind =
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            structured.value.structured_inline_asm.operands[2].operand_index = 2U;
            status = lower_expression(
                context, input->expression, &structured.value.structured_inline_asm.operands[2].value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 2U && source->input_count == 1U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        const MinicInlineAsmOperand *input;
        const MinicInlineAsmOperand *memory_output;
        const MinicInlineAsmOperand *register_output;
        const MinicExpression *input_expression;
        const MinicExpression *memory_expression;
        const MinicExpression *register_expression;
        const MinicLocal *register_local;
        MinicCoreValueId input_value;
        MinicCoreValueId memory_address;
        MinicCoreValueId output_address;
        MinicCoreValueId output_value;
        MinicCoreLowerStatus status;
        MinicType input_type;
        MinicType memory_type;
        MinicType output_type;
        size_t memory_index;
        size_t register_index;

        input = &source->inputs[0];
        memory_output = NULL;
        register_output = NULL;
        memory_index = SIZE_MAX;
        register_index = SIZE_MAX;
        for (size_t output_index = 0U; output_index < 2U; ++output_index) {
            const MinicInlineAsmOperand *candidate;

            candidate = &source->outputs[output_index];
            if (candidate->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(candidate, "+A")) {
                if (memory_output != NULL) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                memory_output = candidate;
                memory_index = output_index;
            } else if (candidate->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_register_output_constraint(candidate)) {
                if (register_output != NULL) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                register_output = candidate;
                register_index = output_index;
            } else {
                memory_output = NULL;
                register_output = NULL;
                break;
            }
        }
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        memory_expression = memory_output == NULL
                                ? NULL
                                : minic_c0_program_expression(context->body->program,
                                                              memory_output->expression);
        register_expression = register_output == NULL
                                  ? NULL
                                  : minic_c0_program_expression(context->body->program,
                                                                register_output->expression);
        if (memory_output != NULL && register_output != NULL &&
            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            core_inline_asm_constraint_is(input, "r") && input_expression != NULL &&
            memory_expression != NULL && register_expression != NULL &&
            memory_expression->value_category == MINIC_VALUE_LVALUE &&
            register_expression->kind == MINIC_EXPRESSION_LOCAL &&
            register_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(memory_expression->type) &&
            !minic_type_is_const(register_expression->type) &&
            !minic_type_is_volatile(register_expression->type) &&
            minic_type_unqualified(memory_expression->type, &memory_type) &&
            minic_type_unqualified(register_expression->type, &output_type) &&
            core_memory_scalar_type(memory_type) && core_memory_scalar_type(output_type) &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type) &&
            minic_type_equal(memory_type, input_type) && minic_type_equal(output_type, memory_type)) {
            register_local = minic_c0_program_local(
                context->body->program, register_expression->value.local_id);
            if (register_local == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!register_local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, register_expression->value.local_id) == NULL &&
                minic_type_equal(register_local->type, register_expression->type)) {
                status = lower_expression(context, input->expression, &input_value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                status = lower_address(context, memory_output->expression, &memory_address);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                if (input_value >= context->function->value_count ||
                    memory_address >= context->function->value_count ||
                    !minic_type_equal(context->function->values[input_value].type, input_type)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               source->template_text,
                                                               source->template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM;
                instruction.span = statement->span;
                instruction.type = output_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.memory_readwrite_scalar_input_inline_asm.inline_asm_id =
                    inline_asm_id;
                instruction.value.memory_readwrite_scalar_input_inline_asm.memory_address =
                    memory_address;
                instruction.value.memory_readwrite_scalar_input_inline_asm.operand = input_value;
                instruction.value.memory_readwrite_scalar_input_inline_asm.memory_operand_index =
                    memory_index;
                instruction.value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index =
                    register_index;
                instruction.value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index =
                    2U;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &output_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                status = lower_address(context, register_output->expression, &output_address);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.store.address = output_address;
                instruction.value.store.stored_value = output_value;
                instruction.value.store.is_volatile = false;
                return minic_core_function_append_effect_instruction(
                           context->function, context->block_id, &instruction)
                           ? MINIC_CORE_LOWER_OK
                           : MINIC_CORE_LOWER_ERROR;
            }
        }
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count == 1U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        const MinicInlineAsmOperand *input;
        const MinicInlineAsmOperand *memory_output;
        const MinicExpression *input_expression;
        const MinicExpression *memory_expression;
        MinicCoreValueId input_value;
        MinicCoreValueId memory_address;
        MinicCoreLowerStatus status;
        MinicType input_type;
        MinicType memory_type;

        memory_output = &source->outputs[0];
        input = &source->inputs[0];
        memory_expression =
            minic_c0_program_expression(context->body->program, memory_output->expression);
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        if (memory_output->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            core_inline_asm_constraint_is(memory_output, "+A") &&
            core_inline_asm_constraint_is(input, "r") && memory_expression != NULL &&
            input_expression != NULL && memory_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(memory_expression->type) &&
            minic_type_unqualified(memory_expression->type, &memory_type) &&
            core_memory_scalar_type(memory_type) &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type) &&
            minic_type_equal(memory_type, input_type)) {
            status = lower_expression(context, input->expression, &input_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = lower_address(context, memory_output->expression, &memory_address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (input_value >= context->function->value_count ||
                memory_address >= context->function->value_count ||
                !minic_type_equal(context->function->values[input_value].type, input_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                           source->template_text,
                                                           source->template_length,
                                                           source->is_volatile,
                                                           source->has_memory_clobber,
                                                           &inline_asm_id)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.memory_readwrite_scalar_input_inline_asm.inline_asm_id =
                inline_asm_id;
            instruction.value.memory_readwrite_scalar_input_inline_asm.memory_address =
                memory_address;
            instruction.value.memory_readwrite_scalar_input_inline_asm.operand = input_value;
            instruction.value.memory_readwrite_scalar_input_inline_asm.memory_operand_index = 0U;
            instruction.value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index =
                SIZE_MAX;
            instruction.value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index =
                1U;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count == 1U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        const MinicInlineAsmOperand *input;
        const MinicInlineAsmOperand *output;
        const MinicExpression *input_expression;
        const MinicExpression *output_expression;
        const MinicLocal *local;
        MinicCoreValueId address_id;
        MinicCoreValueId input_value;
        MinicCoreValueId output_value;
        MinicCoreLowerStatus status;
        MinicType input_type;
        MinicType output_type;
        bool input_register_constraint;
        bool output_register_constraint;

        output = &source->outputs[0];
        input = &source->inputs[0];
        output_expression = minic_c0_program_expression(context->body->program, output->expression);
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        output_register_constraint =
            output->constraint_text != NULL &&
            ((output->constraint_length == 2U &&
              memcmp(output->constraint_text, "=r", 2U) == 0) ||
             (output->constraint_length == 3U &&
              memcmp(output->constraint_text, "=&r", 3U) == 0));
        input_register_constraint =
            input->constraint_text != NULL &&
            ((input->constraint_length == 1U && memcmp(input->constraint_text, "r", 1U) == 0) ||
             (input->constraint_length == 2U && memcmp(input->constraint_text, "rK", 2U) == 0));
        if (output->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            output_register_constraint && input_register_constraint &&
            output_expression != NULL && output_expression->kind == MINIC_EXPRESSION_LOCAL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            !minic_type_is_volatile(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type) && input_expression != NULL &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
            local = minic_c0_program_local(
                context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, output_expression->value.local_id) == NULL &&
                minic_type_equal(local->type, output_expression->type)) {
                status = lower_expression(context, input->expression, &input_value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                if (input_value >= context->function->value_count ||
                    !minic_type_equal(context->function->values[input_value].type, input_type)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               source->template_text,
                                                               source->template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM;
                instruction.span = statement->span;
                instruction.type = output_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.register_output_input_inline_asm.inline_asm_id = inline_asm_id;
                instruction.value.register_output_input_inline_asm.operand = input_value;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &output_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (lower_address(context, output->expression, &address_id) != MINIC_CORE_LOWER_OK) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.store.address = address_id;
                instruction.value.store.stored_value = output_value;
                instruction.value.store.is_volatile = false;
                return minic_core_function_append_effect_instruction(
                           context->function, context->block_id, &instruction)
                           ? MINIC_CORE_LOWER_OK
                           : MINIC_CORE_LOWER_ERROR;
            }
        }
    }

    /* BATCH_L_STRUCTURED_REGISTER_READWRITE: after compile-time i/I inputs
       are specialized into target text, preserve a +r operand as one
       address-backed read/write register binding. Register-clobber names stay
       opaque in Core and are interpreted only by the target backend when it
       chooses operand registers. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count != 0U &&
        source->label_count == 0U && !source->has_memory_clobber &&
        source->clobber_count == source->register_clobber_count) {
        const MinicInlineAsmOperand *output = &source->outputs[0];
        const MinicExpression *output_expression =
            minic_c0_program_expression(context->body->program, output->expression);
        MinicType output_type;
        char *specialized_template = NULL;
        size_t specialized_length = 0U;

        if (output->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
            core_inline_asm_constraint_is(output, "+r") &&
            output_expression != NULL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type) &&
            core_inline_asm_specialize_register_output_immediates(
                context, source, &specialized_template, &specialized_length)) {
            MinicCoreInstruction structured;
            MinicCoreStructuredInlineAsmOperand *binding;
            MinicCoreLowerStatus status;
            size_t clobber_index;
            bool added;

            added = minic_core_function_add_opaque_inline_asm(context->function,
                                                               specialized_template,
                                                               specialized_length,
                                                               true,
                                                               false,
                                                               &inline_asm_id);
            free(specialized_template);
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            for (clobber_index = 0U; clobber_index < source->register_clobber_count;
                 ++clobber_index) {
                const MinicInlineAsmRegisterClobber *clobber =
                    &source->register_clobbers[clobber_index];
                if (clobber->name == NULL || clobber->name_length == 0U ||
                    !minic_core_function_add_inline_asm_register_clobber(
                        context->function,
                        inline_asm_id,
                        clobber->name,
                        clobber->name_length)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }

            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 1U;
            binding = &structured.value.structured_inline_asm.operands[0];
            binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
            binding->operand_index = 0U;
            status = lower_address(context, output->expression, &binding->value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(specialized_template);
    }

    /* BATCH_I_REGISTER_OUTPUT_IMMEDIATE_SPECIALIZATION: after all i/I
       inputs are baked into the template, the runtime shape is exactly the
       existing one-register-output instruction. Core has no optimizer that can
       discard value-producing asm, so retain the specialized instruction in the
       existing execution-effect table; this does not add source-level volatile
       semantics or target-specific IR. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count != 0U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 0U && !source->has_memory_clobber) {
        const MinicInlineAsmOperand *output;
        const MinicExpression *output_expression;
        const MinicLocal *local;
        MinicCoreValueId address_id;
        MinicCoreValueId output_value;
        MinicType output_type;
        char *specialized_template;
        size_t specialized_length;
        bool register_constraint;

        output = &source->outputs[0];
        output_expression = minic_c0_program_expression(context->body->program, output->expression);
        register_constraint =
            output->constraint_text != NULL &&
            ((output->constraint_length == 2U &&
              memcmp(output->constraint_text, "=r", 2U) == 0) ||
             (output->constraint_length == 3U &&
              memcmp(output->constraint_text, "=&r", 3U) == 0));
        specialized_template = NULL;
        specialized_length = 0U;
        if (output->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY && register_constraint &&
            output_expression != NULL && output_expression->kind == MINIC_EXPRESSION_LOCAL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            !minic_type_is_volatile(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type) &&
            core_inline_asm_specialize_register_output_immediates(
                context, source, &specialized_template, &specialized_length)) {
            local = minic_c0_program_local(
                context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                free(specialized_template);
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, output_expression->value.local_id) == NULL &&
                minic_type_equal(local->type, output_expression->type)) {
                /* The specialized text contains only the runtime output %0 and
                   literal %% escapes. Retain it as an execution effect because
                   its SSA result is semantically required. */
                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               specialized_template,
                                                               specialized_length,
                                                               true,
                                                               false,
                                                               &inline_asm_id)) {
                    free(specialized_template);
                    return MINIC_CORE_LOWER_ERROR;
                }
                free(specialized_template);
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM;
                instruction.span = statement->span;
                instruction.type = output_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.inline_asm_id = inline_asm_id;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &output_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (lower_address(context, output->expression, &address_id) != MINIC_CORE_LOWER_OK) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.store.address = address_id;
                instruction.value.store.stored_value = output_value;
                instruction.value.store.is_volatile = false;
                return minic_core_function_append_effect_instruction(
                           context->function, context->block_id, &instruction)
                           ? MINIC_CORE_LOWER_OK
                           : MINIC_CORE_LOWER_ERROR;
            }
        }
        free(specialized_template);
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL &&
        source->output_count == 1U && source->input_count == 0U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        const MinicInlineAsmOperand *output;
        const MinicExpression *output_expression;
        const MinicLocal *local;
        MinicCoreValueId address_id;
        MinicCoreValueId output_value;
        MinicType output_type;
        bool register_constraint;

        output = &source->outputs[0];
        output_expression = minic_c0_program_expression(context->body->program, output->expression);
        register_constraint =
            output->constraint_text != NULL &&
            ((output->constraint_length == 2U &&
              memcmp(output->constraint_text, "=r", 2U) == 0) ||
             (output->constraint_length == 3U &&
              memcmp(output->constraint_text, "=&r", 3U) == 0));
        if (output->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY && register_constraint &&
            output_expression != NULL && output_expression->kind == MINIC_EXPRESSION_LOCAL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            !minic_type_is_volatile(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type)) {
            local = minic_c0_program_local(
                context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, output_expression->value.local_id) == NULL &&
                minic_type_equal(local->type, output_expression->type)) {
                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               source->template_text,
                                                               source->template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM;
                instruction.span = statement->span;
                instruction.type = output_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.inline_asm_id = inline_asm_id;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &output_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (lower_address(context, output->expression, &address_id) != MINIC_CORE_LOWER_OK) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.store.address = address_id;
                instruction.value.store.stored_value = output_value;
                instruction.value.store.is_volatile = false;
                return minic_core_function_append_effect_instruction(
                           context->function, context->block_id, &instruction)
                           ? MINIC_CORE_LOWER_OK
                           : MINIC_CORE_LOWER_ERROR;
            }
        }
    }

    /* M77_EMPTY_TIED_ASM_COPY: an empty, nonvolatile GNU asm with one
       register output tied to input 0 carries no target instruction semantics.
       It preserves the input register bit-pattern in the output. Model that
       target-neutrally as scalar bitcast/copy plus the output store. */
    if (!source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count == 1U && source->label_count == 0U &&
        source->clobber_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber) {
        const MinicInlineAsmOperand *input = &source->inputs[0];
        const MinicInlineAsmOperand *output = &source->outputs[0];
        const MinicExpression *input_expression;
        const MinicExpression *output_expression;
        MinicCoreInstruction store;
        MinicCoreLowerStatus status;
        MinicCoreValueId input_value;
        MinicCoreValueId output_address;
        MinicCoreValueId output_value;
        MinicType input_type;
        MinicType output_type;

        input_expression =
            minic_c0_program_expression(context->body->program, input->expression);
        output_expression =
            minic_c0_program_expression(context->body->program, output->expression);
        if (output->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
            core_inline_asm_register_output_constraint(output) &&
            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            core_inline_asm_constraint_is(input, "0") && output_expression != NULL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type) && input_expression != NULL &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
            status = lower_expression(context, input->expression, &input_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = append_scalar_bitcast(
                context, statement->span, output_type, input_value, &output_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = lower_address(context, output->expression, &output_address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&store, 0, sizeof(store));
            store.kind = MINIC_CORE_INSTRUCTION_STORE;
            store.span = statement->span;
            store.type = minic_type_void();
            store.result = MINIC_CORE_VALUE_INVALID;
            store.value.store.address = output_address;
            store.value.store.stored_value = output_value;
            store.value.store.is_volatile = minic_type_is_volatile(output_expression->type);
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &store)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    if (!source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->outputs != NULL && source->output_count == 1U &&
        source->input_count == 0U && source->label_count == 0U && source->clobber_count == 0U &&
        source->register_clobber_count == 0U && !source->has_memory_clobber) {
        const MinicInlineAsmOperand *output;
        const MinicExpression *output_expression;
        const MinicLocal *local;

        output = &source->outputs[0];
        output_expression = minic_c0_program_expression(context->body->program, output->expression);
        if (output->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
            output->constraint_text != NULL && output->constraint_length == 3U &&
            memcmp(output->constraint_text, "+rm", 3U) == 0 && output_expression != NULL &&
            output_expression->kind == MINIC_EXPRESSION_LOCAL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            core_memory_scalar_type(output_expression->type) &&
            !minic_type_is_const(output_expression->type) &&
            !minic_type_is_volatile(output_expression->type)) {
            local =
                minic_c0_program_local(context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!local->is_array && !local->is_register_storage &&
                minic_type_equal(local->type, output_expression->type) &&
                !minic_type_is_const(local->type) && !minic_type_is_volatile(local->type)) {
                return MINIC_CORE_LOWER_OK;
            }
        }
    }

    /* M89_EMPTY_VOLATILE_OPAQUE_ASM: `asm volatile("")` carries a
       sequencing/volatile effect but intentionally emits no target text. Keep
       the effect explicitly in Core; do not invent a memory clobber. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->output_count == 0U &&
        source->input_count == 0U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->clobber_count == 0U &&
        !source->has_memory_clobber) {
        if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                       source->template_text,
                                                       0U,
                                                       true,
                                                       false,
                                                       &inline_asm_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
        instruction.span = statement->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.inline_asm_id = inline_asm_id;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

    /* M59_EMPTY_SCALAR_INPUT_BARRIER: GNU barrier_data() is an empty
       volatile asm with one scalar register input and a memory clobber. The
       operand must still be evaluated, but an empty target template needs no
       target instruction. Represent the ordering effect with the existing
       target-neutral compiler barrier rather than inventing an empty opaque
       asm encoding. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 1U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->has_memory_clobber &&
        source->clobber_count == 1U) {
        const MinicInlineAsmOperand *input;
        const MinicExpression *input_expression;
        MinicCoreValueId discarded_input;
        MinicCoreLowerStatus input_status;
        MinicType input_type;

        input = &source->inputs[0];
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        if (input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            (core_inline_asm_constraint_is(input, "r") ||
             core_inline_asm_constraint_is(input, "rK")) &&
            input_expression != NULL &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
            input_status = lower_expression(context, input->expression, &discarded_input);
            if (input_status != MINIC_CORE_LOWER_OK) {
                return input_status;
            }
            if (discarded_input >= context->function->value_count ||
                !minic_type_equal(context->function->values[discarded_input].type, input_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    /* BATCH_X_TWO_SCALAR_OUTPUTLESS_ASM_OPTIONAL_MEMORY: outputless GNU asm is
       effectively volatile (Batch F).  The two-register-input structured form
       is valid both for ordering-sensitive asm carrying a memory clobber and
       for MMIO-style asm whose template itself performs the access.  Preserve
       the actual memory effect flag rather than requiring one to exist. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 2U && source->label_count == 0U &&
        source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t input_index;
        bool supported_shape = true;

        for (input_index = 0U; input_index < 2U; ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *input_expression = minic_c0_program_expression(
                context->body->program, operand->expression);
            MinicType input_type;
            bool register_constraint;

            register_constraint =
                operand->constraint_text != NULL &&
                ((operand->constraint_length == 1U &&
                  memcmp(operand->constraint_text, "r", 1U) == 0) ||
                 (operand->constraint_length == 2U &&
                  memcmp(operand->constraint_text, "rK", 2U) == 0));
            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                !register_constraint || input_expression == NULL ||
                !core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
                supported_shape = false;
                break;
            }
        }
        if (supported_shape &&
            core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added;

            added = numeric_template_length != 0U &&
                    minic_core_function_add_opaque_inline_asm(context->function,
                                                              numeric_template,
                                                              numeric_template_length,
                                                              true,
                                                              source->has_memory_clobber,
                                                              &inline_asm_id);
            free(numeric_template);
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 2U;
            for (input_index = 0U; input_index < 2U; ++input_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[input_index];
                MinicCoreLowerStatus status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = input_index;
                status = lower_expression(context, source->inputs[input_index].expression,
                                          &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 1U && source->label_count == 0U &&
        source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        const MinicInlineAsmOperand *input;
        const MinicExpression *input_expression;
        MinicCoreValueId input_value;
        MinicCoreLowerStatus input_status;
        MinicType input_type;
        bool register_constraint;

        input = &source->inputs[0];
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        register_constraint =
            input->constraint_text != NULL &&
            ((input->constraint_length == 1U &&
              memcmp(input->constraint_text, "r", 1U) == 0) ||
             (input->constraint_length == 2U &&
              memcmp(input->constraint_text, "rK", 2U) == 0));
        if (input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY && register_constraint &&
            input_expression != NULL &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
            input_status = lower_expression(context, input->expression, &input_value);
            if (input_status != MINIC_CORE_LOWER_OK) {
                return input_status;
            }
            if (input_value >= context->function->value_count ||
                !minic_type_equal(context->function->values[input_value].type, input_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                           source->template_text,
                                                           source->template_length,
                                                           true,
                                                           source->has_memory_clobber,
                                                           &inline_asm_id)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.scalar_input_inline_asm.inline_asm_id = inline_asm_id;
            instruction.value.scalar_input_inline_asm.operand = input_value;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    /* M87_IMMEDIATE_ASM_FRONTIER_TRACE: report details only after every
       supported inline-asm path above has declined the statement. This keeps
       frontier observability from becoming a false first-error locator. */
    if (source->is_volatile && !source->is_goto && source->output_count == 0U &&
        source->input_count != 0U && source->inputs != NULL && source->label_count == 0U) {
        size_t trace_input_index;

        (void)fprintf(stderr,
                      "CORE_ASM_DETAIL reason=unclaimed function=%s inputs=%zu "
                      "reg_clobbers=%zu clobbers=%zu memory=%d template_length=%zu\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      source->input_count,
                      source->register_clobber_count,
                      source->clobber_count,
                      source->has_memory_clobber ? 1 : 0,
                      source->template_length);
        for (trace_input_index = 0U; trace_input_index < source->input_count; ++trace_input_index) {
            const MinicInlineAsmOperand *trace_operand = &source->inputs[trace_input_index];
            const MinicExpression *trace_expression = minic_c0_program_expression(
                context->body->program, trace_operand->expression);
            char trace_integer_text[MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
            const char *trace_resolved_text = NULL;
            size_t trace_resolved_length = 0U;
            bool trace_resolved = core_inline_asm_immediate_text(
                context,
                trace_operand,
                trace_integer_text,
                sizeof(trace_integer_text),
                &trace_resolved_text,
                &trace_resolved_length);
            (void)trace_resolved_text;
            (void)fprintf(stderr,
                          "CORE_ASM_DETAIL input function=%s index=%zu constraint=%.*s "
                          "access=%d expr_kind=%d immediate_resolved=%d resolved_length=%zu\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          trace_input_index,
                          (int)trace_operand->constraint_length,
                          trace_operand->constraint_text != NULL ? trace_operand->constraint_text : "",
                          (int)trace_operand->access,
                          trace_expression != NULL ? (int)trace_expression->kind : -1,
                          trace_resolved ? 1 : 0,
                          trace_resolved_length);
        }
    }

    if (!source->is_volatile || source->is_goto || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 0U || source->input_count != 0U ||
        source->label_count != 0U || source->register_clobber_count != 0U) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                   source->template_text,
                                                   source->template_length,
                                                   source->is_volatile,
                                                   source->has_memory_clobber,
                                                   &inline_asm_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
    instruction.span = statement->span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.inline_asm_id = inline_asm_id;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

#define MINIC_CORE_SWITCH_LABEL_LIMIT 128U

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
    if (lower_expression == NULL || lower_expression->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(lower_expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = reload_scalar_value(
        context, case_statement->span, selector_type, selector_object, &selector);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = append_switch_integer_constant(context,
                                            lower_expression->span,
                                            selector_type,
                                            lower_expression->value.integer_value,
                                            &bound);
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
    if (upper_expression == NULL || upper_expression->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(upper_expression->type)) {
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
        status = append_switch_integer_constant(context,
                                                upper_expression->span,
                                                selector_type,
                                                upper_expression->value.integer_value,
                                                &bound);
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
        statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        statement->expression == MINIC_EXPRESSION_INVALID ||
        statement->then_block == MINIC_BLOCK_INVALID ||
        statement->else_block != MINIC_BLOCK_INVALID || context->target == NULL) {
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
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            continue;
        }
        if (source_statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
            source_statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
            source_statement->then_block != MINIC_BLOCK_INVALID ||
            source_statement->else_block != MINIC_BLOCK_INVALID ||
            label_count >= MINIC_CORE_SWITCH_LABEL_LIMIT) {
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
        size_t segment_begin;
        size_t segment_end;
        size_t scan;
        MinicCoreBlockId saved_break_target;
        bool segment_terminated;

        segment_begin = labels[source_index].source_index + 1U;
        segment_end = source_index + 1U < label_count ? labels[source_index + 1U].source_index
                                                      : body->statement_count;
        break_index = SIZE_MAX;
        for (scan = segment_begin; scan < segment_end; ++scan) {
            const MinicStatement *segment_statement;

            segment_statement =
                minic_c0_program_statement(context->body->program, body->statements[scan]);
            if (segment_statement == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (segment_statement->kind == MINIC_STATEMENT_BREAK) {
                if (break_index != SIZE_MAX || scan + 1U != segment_end ||
                    segment_statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
                    segment_statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                break_index = scan;
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
        if (block_terminated) {
            /* Parser scope exit materializes the same cleanup expression for
               ordinary fallthrough. A return edge above has already consumed
               that cleanup, so the unreachable duplicate must not run again.
               Keep all other unreachable expression statements fail-closed. */
            if (statement->kind == MINIC_STATEMENT_RETURN ||
                core_is_materialized_cleanup_statement(context, statement)) {
                continue;
            }
            if (statement->kind != MINIC_STATEMENT_LABEL) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        }
        /* BATCH_C_ZERO_DISTANCE_CLEANUP_EDGE: cleanup ids are semantic edge
           metadata. Equal ids mean the edge crosses no cleanup lifetime, even
           when both ids are non-root. Only an actual context transition needs
           cleanup-expression lowering, which remains fail-closed here. */
        if (statement->cleanup_context != statement->cleanup_stop_context &&
            statement->kind != MINIC_STATEMENT_RETURN) {
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
                status = lower_opaque_inline_asm(context, statement);
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
                status = set_branch(
                    context, context->block_id, statement->span, context->break_target);
                statement_terminated = status == MINIC_CORE_LOWER_OK;
                break;
            case MINIC_STATEMENT_GOTO: {
                const MinicStatement *target_statement;
                MinicCoreBlockId target_block;

                if (statement->target_expression != MINIC_EXPRESSION_INVALID ||
                    statement->expression != MINIC_EXPRESSION_INVALID ||
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
    if (!minic_core_function_verify(&lowered)) {
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_ERROR;
    }
    minic_core_function_destroy(output);
    *output = lowered;
    return MINIC_CORE_LOWER_OK;
}
