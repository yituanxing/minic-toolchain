#include "core/core_lower_internal.h"
#include "core/local_facts.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* AST rvalue dispatch owns source evaluation order, scalar transformations,
 * and the handoff into effectful Core lowering families. No target encoding. */

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
    /* GNU statement expressions are an effectful Core CFG family, not a
       general scalar conversion. Retain this priority before conditionals. */
    {
        bool handled = false;
        MinicCoreLowerStatus status = minic_core_lower_try_statement_expression(
            context, expression, value_id, &handled);
        if (handled) return status;
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
    /* Arithmetic lowering is an independent source-semantic family. */
    {
        bool handled = false;
        MinicCoreLowerStatus status = minic_core_lower_try_arithmetic_unary(
            context, expression, value_id, &handled);
        if (handled) return status;
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
            return minic_core_lower_internal_append_long_double_conversion(
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
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_ARG)
        return minic_core_lower_vararg_scalar(context, expression, value_id);

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
    /* Arithmetic lowering is an independent source-semantic family. */
    {
        bool handled = false;
        MinicCoreLowerStatus status = minic_core_lower_try_arithmetic_binary(
            context, expression, value_id, &handled);
        if (handled) return status;
    }
    /* Bit-field RMW keeps one-evaluation/volatile semantics intact. */
    {
        bool matched = false;
        MinicCoreLowerStatus status =
            minic_core_lower_try_bit_field_compound(context, expression, value_id, &matched);
        if (matched || status != MINIC_CORE_LOWER_OK) return status;
    }
    /* Arithmetic lowering is an independent source-semantic family. */
    {
        bool handled = false;
        MinicCoreLowerStatus status = minic_core_lower_try_arithmetic_compound(
            context, expression, value_id, &handled);
        if (handled) return status;
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
