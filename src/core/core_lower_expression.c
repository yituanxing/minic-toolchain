#include "core/core_lower_internal.h"
#include "core/source_loop_shapes.h"
#include "core/local_facts.h"
#include "core/cfg_constant_calls.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Whole source expression lowering and expression-statement effect family.
 * This module evaluates C operand effects into typed Core IR, while
 * core_lower.c keeps function, statement CFG, cleanup and return ownership.
 * Target ABI and machine selection remain in Target. */

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
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_START ||
        expression->kind == MINIC_EXPRESSION_BUILTIN_VA_COPY ||
        expression->kind == MINIC_EXPRESSION_BUILTIN_VA_END)
        return minic_core_lower_vararg_statement(context, expression);

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
        return minic_core_lower_record_copy_statement(context, &record_copy);
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
