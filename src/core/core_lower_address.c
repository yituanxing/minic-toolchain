#include "core/core_lower_internal.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Core lvalue/address lowering owns source address effects, not machine ABI. */

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
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreValueId base_address;

        /* An array field of a record rvalue is an array object in the
         * enclosing record's full-expression temporary, not an lvalue in
         * the source AST. Array decay/subscript may request its backing
         * address; materialize the one record before projecting the field. */
        if (expression->value_category != MINIC_VALUE_RVALUE ||
            expression->kind != MINIC_EXPRESSION_MEMBER ||
            !minic_c0_expression_array_object_info(
                context->body->program, expression, NULL))
            return MINIC_CORE_LOWER_UNSUPPORTED;
        base = minic_c0_program_expression(
            context->body->program, expression->value.member.base);
        record = minic_c0_program_record(
            context->body->program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (base == NULL || field == NULL ||
            (!field->is_array && !minic_type_is_array(field->type)))
            return MINIC_CORE_LOWER_UNSUPPORTED;
        status = minic_core_lower_internal_record_materialized_address(
            context, expression->value.member.base, &base_address);
        if (status != MINIC_CORE_LOWER_OK)
            return status;
        return minic_core_lower_internal_append_field_address(
            context, expression->span, base_address,
            expression->value.member.record_id,
            expression->value.member.field_index, expression->type, address_id);
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
            status = minic_core_lower_internal_local_object(
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
        status = minic_core_lower_internal_local_object(
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
        status = minic_core_lower_record_compound_literal_object(context, expression, &object_id);
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
        if (local->is_array && local->element_count == 0U &&
            !local->is_zero_length_array) {
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
            status = minic_core_lower_internal_local_object(
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

        status = minic_core_lower_internal_local_object(context, expression->value.local_id, &object_id);
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
        const MinicExpression *pointer_expression = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        MinicCoreValueId pointer_id;
        MinicType expected_pointer;
        MinicType pointer_value_type;

        if (!minic_type_pointer_to(expression->type, &expected_pointer)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* In an lvalue address context, *&object designates precisely the
         * original object. Keep this cancellation at the common Core address
         * owner, so GNU memory constraints (including x87 floating-point
         * globals), ordinary C address-taking and all ISA backends agree.
         * Recursing into lower_address of the object retains side effects
         * of member/subscript address computations and its exact qualifiers;
         * no value load or runtime pointer SSA is semantically needed. */
        if (pointer_expression != NULL &&
            pointer_expression->kind == MINIC_EXPRESSION_ADDRESS_OF &&
            minic_type_equal(pointer_expression->type, expected_pointer)) {
            const MinicExpression *object_expression = minic_c0_program_expression(
                context->body->program,
                pointer_expression->value.unary.operand);
            if (object_expression != NULL &&
                object_expression->value_category == MINIC_VALUE_LVALUE &&
                minic_type_equal(object_expression->type, expression->type))
                return lower_address(context,
                    pointer_expression->value.unary.operand, address_id);
        }
        status = lower_expression(context, expression->value.unary.operand, &pointer_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        /* GNU's Xen RING_COPY_RESPONSE uses
         * `*(volatile typeof(dest))RING_GET_RESPONSE(...)`: `typeof(dest)` is
         * a pointer and its outer volatile qualifies the pointer VALUE,
         * not the pointed-to record. A dereference consumes the unqualified
         * pointer value. Preserve exact pointee qualifiers and shape; strip
         * only the top-level pointer value cv-qualification here. */
        if (pointer_id >= context->function->value_count ||
            !minic_type_unqualified(context->function->values[pointer_id].type,
                                    &pointer_value_type) ||
            !minic_type_equal(pointer_value_type, expected_pointer)) {
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
        bool pure_global_element_zero;

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
        /* An array's static global address plus a literal zero index is a
         * pure symbolic address. Its Core GLOBAL_ADDRESS provenance must not
         * be converted into a local STORE/LOAD just to sequence an index
         * expression that has no effects. In particular this preserves the
         * linker-visible identity of ordinary C string literals passed to
         * GNU `i` operands. Dynamic indices and pointer-valued bases retain
         * the established evaluation-order snapshot contract. */
        pure_global_element_zero =
            array_base && base->kind == MINIC_EXPRESSION_GLOBAL_OBJECT &&
            index->kind == MINIC_EXPRESSION_INTEGER &&
            index->value.integer_value == 0;
        if (!pure_global_element_zero) {
            subscript_status = minic_core_lower_internal_spill_scalar_value(
                context, base->span, pointer_type, base_value, &base_object);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
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
        if (!pure_global_element_zero) {
            subscript_status = minic_core_lower_internal_reload_scalar_value(
                context, base->span, pointer_type, base_object, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                (void)fprintf(stderr,
                              "CORE_SUBSCRIPT_STAGE function=%s stage=reload-base status=%d\n",
                              context->source_function != NULL ? context->source_function->name : "?",
                              (int)subscript_status);
                return subscript_status;
            }
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
