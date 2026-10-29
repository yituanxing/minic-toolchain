#include "core/core_lower_asm_internal.h"

#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include "target/data_layout.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GNU inline-asm source operand interpretation and template normalization.
 * Target machine dialects remain opaque; normalization creates no Core effects. */

bool core_inline_asm_constraint_is(const MinicInlineAsmOperand *operand,
                                              const char *text) {
    size_t length;

    if (operand == NULL || text == NULL || operand->constraint_text == NULL) {
        return false;
    }
    length = strlen(text);
    return operand->constraint_length == length &&
           memcmp(operand->constraint_text, text, length) == 0;
}

bool core_inline_asm_register_output_constraint(const MinicInlineAsmOperand *operand) {
    return core_inline_asm_constraint_is(operand, "=r") ||
           core_inline_asm_constraint_is(operand, "=&r");
}

/* M105_FIXED_REGISTER_STRUCTURED_ASM: preflight resolves both a declared
 * `register T local asm("rN")` and the dedicated FIXED_REGISTER expression
 * used by global hardware bindings (for example x86 Linux RSP). No register
 * spelling or ISA-specific policy enters Core: the target must qualify the
 * exact ASM contract before this binding is ever imported. */
bool core_inline_asm_fixed_binding_id(const MinicC0Program *program,
                                                   const MinicExpression *expression,
                                                   size_t *binding_id) {
    const MinicFixedRegisterBinding *binding;
    size_t index;

    if (program == NULL || expression == NULL || binding_id == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_FIXED_REGISTER) {
        *binding_id = expression->value.fixed_register_binding_id;
        binding = minic_c0_program_fixed_register_binding(program, *binding_id);
        return binding != NULL && binding->register_name != NULL &&
               binding->register_name_length != 0U;
    }
    if (expression->kind != MINIC_EXPRESSION_LOCAL) return false;
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



static bool core_inline_asm_parameter_index(
    const MinicCoreLowerContext *context,
    MinicLocalId local_id,
    size_t *parameter_index) {
    const MinicFunction *source;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->source_function == NULL || parameter_index == NULL ||
        !context->source_function->is_integer_specialization ||
        context->source_function->specialization_source >=
            context->body->program->function_count) {
        return false;
    }
    source = &context->body->program->functions[
        context->source_function->specialization_source];
    if (local_id < source->local_begin) {
        return false;
    }
    *parameter_index = local_id - source->local_begin;
    return *parameter_index < source->parameter_count &&
           *parameter_index < MINIC_MAX_FUNCTION_PARAMETERS;
}

static bool core_inline_asm_specialized_integer(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    int64_t *value,
    unsigned int depth) {
    const MinicExpression *expression;
    MinicConstValue constant;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || value == NULL || depth > 32U) {
        return false;
    }
    if (minic_const_eval_integer(
            context->body->program, context->target, expression_id, &constant) &&
        minic_const_value_as_int64(
            context->body->program, context->target, &constant, value)) {
        return true;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return core_inline_asm_specialized_integer(
            context, expression->value.unary.operand, value, depth + 1U);
    }
    /* Specialization replaces parameters with compile-time integers, but a
     * GNU `i` operand can still be an expression over those parameters.
     * Resolve the complete pure expression in Core so the machine target
     * receives a constant, never a host-assembler expression or an unbound
     * runtime register.  Reject undefined shifts, division and overflow. */
    if (expression->kind == MINIC_EXPRESSION_BINARY) {
        int64_t left, right;
        if (!core_inline_asm_specialized_integer(
                context, expression->value.binary.left, &left, depth + 1U) ||
            !core_inline_asm_specialized_integer(
                context, expression->value.binary.right, &right, depth + 1U))
            return false;
        switch (expression->value.binary.operator_kind) {
            case MINIC_BINARY_BITWISE_AND: *value = left & right; return true;
            case MINIC_BINARY_BITWISE_OR: *value = left | right; return true;
            case MINIC_BINARY_BITWISE_XOR: *value = left ^ right; return true;
            case MINIC_BINARY_SHIFT_RIGHT:
                if (left < 0 || right < 0 || right >= 63) return false;
                *value = left >> (unsigned)right;
                return true;
            case MINIC_BINARY_SHIFT_LEFT:
                if (left < 0 || right < 0 || right >= 63 ||
                    left > (INT64_MAX >> (unsigned)right)) return false;
                *value = left << (unsigned)right;
                return true;
            case MINIC_BINARY_ADD:
                if ((right > 0 && left > INT64_MAX - right) ||
                    (right < 0 && left < INT64_MIN - right)) return false;
                *value = left + right;
                return true;
            case MINIC_BINARY_SUBTRACT:
                if ((right < 0 && left > INT64_MAX + right) ||
                    (right > 0 && left < INT64_MIN + right)) return false;
                *value = left - right;
                return true;
            case MINIC_BINARY_MULTIPLY:
                if (left != 0 && right != 0 &&
                    (left == INT64_MIN && right == -1)) return false;
                if (right > 0 &&
                    (left > INT64_MAX / right || left < INT64_MIN / right))
                    return false;
                if (right < -1 &&
                    (left > INT64_MIN / right || left < INT64_MAX / right))
                    return false;
                if (right == -1 && left == INT64_MIN) return false;
                *value = left * right;
                return true;
            case MINIC_BINARY_DIVIDE:
                if (right == 0 || (left == INT64_MIN && right == -1))
                    return false;
                *value = left / right;
                return true;
            case MINIC_BINARY_REMAINDER:
                if (right == 0 || (left == INT64_MIN && right == -1))
                    return false;
                *value = left % right;
                return true;
            default: return false;
        }
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        size_t parameter_index;
        const MinicFunction *function = context->source_function;
        const MinicFunction *source;
        MinicConstValue specialized;
        if (!core_inline_asm_parameter_index(
                context, expression->value.local_id, &parameter_index) ||
            !function->specialization_integer_known[parameter_index]) {
            return false;
        }
        source = &context->body->program->functions[function->specialization_source];
        specialized.type = source->parameter_types[parameter_index];
        specialized.bits = function->specialization_integer_bits[parameter_index];
        return minic_const_value_as_int64(
            context->body->program, context->target, &specialized, value);
    }
    return false;
}

static bool core_inline_asm_symbolic_address_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    const char **symbol,
    int64_t *addend,
    unsigned int depth);

static bool core_inline_asm_symbolic_lvalue_address(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    const char **symbol,
    int64_t *addend,
    unsigned int depth) {
    const MinicC0Program *program;
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        symbol == NULL || addend == NULL || depth > 32U) {
        return false;
    }
    program = context->body->program;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *object =
            minic_c0_program_global_object(program, expression->value.global_object_id);
        if (object == NULL || object->name == NULL || object->name_length == 0U) {
            return false;
        }
        *symbol = object->name;
        *addend = 0;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_DEREFERENCE) {
        return core_inline_asm_symbolic_address_depth(
            context,
            expression->value.unary.operand,
            symbol,
            addend,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicRecord *record =
            minic_c0_program_record(program, expression->value.member.record_id);
        const MinicExpression *base =
            minic_c0_program_expression(program, expression->value.member.base);
        size_t field_offset;
        int64_t base_addend;
        bool base_ok;

        if (record == NULL || base == NULL ||
            !minic_data_layout_record_field_offset(
                minic_target_info_data_layout(context->target),
                program,
                record,
                expression->value.member.field_index,
                &field_offset) || field_offset > (size_t)INT64_MAX) {
            return false;
        }
        if (minic_type_is_pointer(base->type)) {
            base_ok = core_inline_asm_symbolic_address_depth(
                context,
                expression->value.member.base,
                symbol,
                &base_addend,
                depth + 1U);
        } else {
            base_ok = core_inline_asm_symbolic_lvalue_address(
                context,
                expression->value.member.base,
                symbol,
                &base_addend,
                depth + 1U);
        }
        if (!base_ok || base_addend > INT64_MAX - (int64_t)field_offset) {
            return false;
        }
        *addend = base_addend + (int64_t)field_offset;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base =
            minic_c0_program_expression(program, expression->value.subscript.base);
        const MinicArrayType *array_type = NULL;
        MinicArrayObjectInfo array_info;
        MinicType element_type;
        size_t element_size;
        size_t element_alignment;
        int64_t index;
        int64_t base_addend;
        int64_t scaled;
        bool base_ok;

        if (base == NULL) {
            return false;
        }
        (void)memset(&array_info, 0, sizeof(array_info));
        if (minic_type_is_pointer(base->type)) {
            if (!minic_type_pointee(base->type, &element_type)) {
                return false;
            }
            base_ok = core_inline_asm_symbolic_address_depth(
                context,
                expression->value.subscript.base,
                symbol,
                &base_addend,
                depth + 1U);
        } else if (minic_c0_expression_array_object_info(
                       program, base, &array_info)) {
            /* Legacy record-member arrays carry their element MinicType
             * plus array-object metadata, while materialized arrays carry
             * MINIC_TYPE_BASE_ARRAY. Both have identical C address
             * semantics. This must mirror ordinary Core subscript lowering
             * or a symbol+constant inline operand silently loses arrays. */
            if (array_info.is_zero_length || array_info.is_incomplete)
                return false;
            if (array_info.has_materialized_type) {
                array_type = minic_c0_program_array_type(
                    program, base->type.array_type_id);
                if (!array_type || array_type->element_count == 0U)
                    return false;
            }
            element_type = array_info.element_type;
            base_ok = core_inline_asm_symbolic_lvalue_address(
                context,
                expression->value.subscript.base,
                symbol,
                &base_addend,
                depth + 1U);
        } else {
            return false;
        }
        if (!base_ok ||
            !minic_data_layout_type(
                minic_target_info_data_layout(context->target),
                program,
                element_type,
                &element_size,
                &element_alignment) ||
            /* A GNU zero-sized element has no address stride. Do not feed
             * that size to the signed overflow division below; an inline
             * relocation cannot assign meaningful offsets to its elements. */
            element_size == 0U ||
            element_size > (size_t)INT64_MAX ||
            !core_inline_asm_specialized_integer(
                context, expression->value.subscript.index, &index, depth + 1U)) {
            return false;
        }
        if (index != 0 &&
            ((index > 0 && index > INT64_MAX / (int64_t)element_size) ||
             (index < 0 && index < INT64_MIN / (int64_t)element_size))) {
            return false;
        }
        scaled = index * (int64_t)element_size;
        if ((scaled > 0 && base_addend > INT64_MAX - scaled) ||
            (scaled < 0 && base_addend < INT64_MIN - scaled)) {
            return false;
        }
        *addend = base_addend + scaled;
        return true;
    }
    return false;
}

static bool core_inline_asm_symbolic_address_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    const char **symbol,
    int64_t *addend,
    unsigned int depth) {
    const MinicC0Program *program;
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || symbol == NULL || addend == NULL || depth > 32U) {
        return false;
    }
    program = context->body->program;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *object =
            minic_c0_program_global_object(program, expression->value.global_object_id);
        if (object == NULL || object->name == NULL || object->name_length == 0U) {
            return false;
        }
        *symbol = object->name;
        *addend = 0;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        const MinicFunction *function =
            minic_c0_program_function(program, expression->value.function_id);
        if (function == NULL) {
            return false;
        }
        *symbol = function->assembler_name != NULL && function->assembler_name_length != 0U
                      ? function->assembler_name
                      : function->name;
        *addend = 0;
        return *symbol != NULL && **symbol != '\0';
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        return core_inline_asm_symbolic_address_depth(
            context,
            expression->value.unary.operand,
            symbol,
            addend,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        const MinicExpression *operand =
            minic_c0_program_expression(program, expression->value.unary.operand);
        if (operand == NULL || operand->kind != MINIC_EXPRESSION_LOCAL) {
            return false;
        }
        return core_inline_asm_symbolic_address_depth(
            context,
            expression->value.unary.operand,
            symbol,
            addend,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        size_t parameter_index;
        MinicExpressionId specialized_expression;
        if (!core_inline_asm_parameter_index(
                context, expression->value.local_id, &parameter_index) ||
            !context->source_function->specialization_symbolic_known[parameter_index]) {
            return false;
        }
        specialized_expression =
            context->source_function->specialization_symbolic_expression[parameter_index];
        if (specialized_expression == MINIC_EXPRESSION_INVALID ||
            specialized_expression == expression_id) {
            return false;
        }
        return core_inline_asm_symbolic_address_depth(
            context, specialized_expression, symbol, addend, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        return core_inline_asm_symbolic_lvalue_address(
            context,
            expression->value.unary.operand,
            symbol,
            addend,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        int64_t integer;
        int64_t base_addend;
        if (core_inline_asm_symbolic_address_depth(
                context,
                expression->value.binary.left,
                symbol,
                &base_addend,
                depth + 1U) &&
            core_inline_asm_specialized_integer(
                context,
                expression->value.binary.right,
                &integer,
                depth + 1U)) {
            if (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT) {
                if (integer == INT64_MIN) {
                    return false;
                }
                integer = -integer;
            }
            if ((integer > 0 && base_addend > INT64_MAX - integer) ||
                (integer < 0 && base_addend < INT64_MIN - integer)) {
                return false;
            }
            *addend = base_addend + integer;
            return true;
        }
    }
    return false;
}

static bool core_inline_asm_specialized_symbolic_text(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    char *buffer,
    size_t capacity,
    const char **text_out,
    size_t *length_out) {
    const char *symbol;
    int64_t addend;
    int written;

    if (buffer == NULL || capacity == 0U || text_out == NULL || length_out == NULL ||
        !core_inline_asm_symbolic_address_depth(
            context, expression_id, &symbol, &addend, 0U)) {
        return false;
    }
    if (addend == 0) {
        *text_out = symbol;
        *length_out = strlen(symbol);
        return *length_out != 0U;
    }
    written = snprintf(buffer,
                       capacity,
                       addend > 0 ? "%s+%" PRId64 : "%s%" PRId64,
                       symbol,
                       addend);
    if (written < 0 || (size_t)written >= capacity) {
        return false;
    }
    *text_out = buffer;
    *length_out = (size_t)written;
    return true;
}


static bool core_inline_asm_local_integer_fact_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicConstValue *value,
    unsigned int depth) {
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || context->source_function == NULL || value == NULL ||
        depth > 16U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        MinicLocalId local_id = expression->value.local_id;
        size_t local_index;
        const MinicCoreLocalIntegerConstant *fact;

        if (context->local_integer_constants == NULL ||
            local_id < context->source_function->local_begin) {
            return false;
        }
        local_index = local_id - context->source_function->local_begin;
        if (local_index >= context->source_function->local_count) {
            return false;
        }
        fact = &context->local_integer_constants[local_index];
        if (!fact->known || fact->escaped) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &fact->value,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        MinicConstValue operand_value;
        if (!core_inline_asm_local_integer_fact_depth(context,
                                                      expression->value.unary.operand,
                                                      &operand_value,
                                                      depth + 1U)) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &operand_value,
                                                 expression->type,
                                                 value);
    }
    return false;
}

static bool core_inline_asm_local_integer_fact(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicConstValue *value) {
    return core_inline_asm_local_integer_fact_depth(context, expression_id, value, 0U);
}


static const char *core_inline_asm_specialized_symbolic_name_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    unsigned int depth) {
    const MinicC0Program *program;
    const MinicExpression *expression;
    const MinicFunction *specialized;
    const MinicFunction *source;
    MinicLocalId local_id;
    size_t parameter_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->source_function == NULL || depth > 16U) {
        return NULL;
    }
    program = context->body->program;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return NULL;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return core_inline_asm_specialized_symbolic_name_depth(
            context, expression->value.unary.operand, depth + 1U);
    }
    if (expression->kind != MINIC_EXPRESSION_LOCAL) {
        return NULL;
    }

    specialized = context->source_function;
    if (!specialized->is_integer_specialization ||
        specialized->specialization_source >= program->function_count) {
        return NULL;
    }
    source = minic_c0_program_function(program, specialized->specialization_source);
    if (source == NULL) {
        return NULL;
    }
    local_id = expression->value.local_id;
    if (local_id < source->local_begin) {
        return NULL;
    }
    parameter_index = local_id - source->local_begin;
    if (parameter_index >= source->parameter_count ||
        parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
        !specialized->specialization_symbolic_known[parameter_index] ||
        specialized->specialization_symbolic_expression[parameter_index] ==
            MINIC_EXPRESSION_INVALID) {
        return NULL;
    }
    return core_inline_asm_symbolic_immediate_name(
        program,
        context->target,
        specialized->specialization_symbolic_expression[parameter_index]);
}

static const char *core_inline_asm_specialized_symbolic_name(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id) {
    return core_inline_asm_specialized_symbolic_name_depth(context, expression_id, 0U);
}

 bool core_inline_asm_immediate_text(
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
         !core_inline_asm_constraint_is(operand, "I") &&
         !core_inline_asm_constraint_is(operand, "rK"))) {
        return false;
    }
    if ((minic_const_eval_integer(
             context->body->program, context->target, operand->expression, &constant) ||
         core_inline_asm_local_integer_fact(context, operand->expression, &constant)) &&
        minic_const_value_as_int64(
            context->body->program, context->target, &constant, &value) &&
        (!core_inline_asm_constraint_is(operand, "rK") ||
         (value >= 0 && value <= 31)) &&
        (!core_inline_asm_constraint_is(operand, "I") ||
         minic_target_info_inline_asm_immediate_constraint_supported(
             context->target, "I", 1U, value))) {
        int written;

        written = snprintf(integer_text, integer_capacity, "%" PRId64, value);
        if (written < 0 || (size_t)written >= integer_capacity) {
            return false;
        }
        *text_out = integer_text;
        *length_out = (size_t)written;
        return true;
    }
    /* Compute expressions such as `1 << (feature & 7)` and `feature >> 3`
     * after transitive inline specialization. This path is also used by
     * relocatable-symbol indexing, and never fabricates an unknown value. */
    if (core_inline_asm_specialized_integer(
            context, operand->expression, &value, 0U) &&
        (!core_inline_asm_constraint_is(operand, "rK") ||
         (value >= 0 && value <= 31)) &&
        (!core_inline_asm_constraint_is(operand, "I") ||
         minic_target_info_inline_asm_immediate_constraint_supported(
             context->target, "I", 1U, value))) {
        int written = snprintf(integer_text, integer_capacity, "%" PRId64, value);
        if (written < 0 || (size_t)written >= integer_capacity) return false;
        *text_out = integer_text;
        *length_out = (size_t)written;
        return true;
    }
    /* Constraint I cannot accept an address even through transitive
     * symbolic specialization. Guard BEFORE the symbolic fast path; only a
     * target-qualified numeric value above may satisfy it. */
    if (core_inline_asm_constraint_is(operand, "I")) {
        return false;
    }
    if (core_inline_asm_specialized_symbolic_text(
            context,
            operand->expression,
            integer_text,
            integer_capacity,
            text_out,
            length_out)) {
        return true;
    }
    if (core_inline_asm_constraint_is(operand, "rK") ||
        core_inline_asm_constraint_is(operand, "I")) {
        /* GNU I is numeric ISA data, never a relocatable address. */
        return false;
    }
    symbol = core_inline_asm_symbolic_immediate_name(
        context->body->program, context->target, operand->expression);
    if (symbol == NULL) {
        symbol = core_inline_asm_specialized_symbolic_name(
            context, operand->expression);
    }
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

/* Parse one GNU compile-time input reference for BOTH sizing and output.
 * The c modifier is the unadorned immediate/address spelling required by
 * original Linux BUG/WARN tables; no architecture-specific text belongs in
 * this Core normalizer. Unsupported modifiers fail closed. */
static bool core_inline_asm_immediate_reference(
    const MinicInlineAsm *source, size_t cursor,
    size_t *index, size_t *consumed) {
    size_t position;
    size_t begin, end;
    if (source == NULL || source->template_text == NULL ||
        index == NULL || consumed == NULL ||
        cursor >= source->template_length ||
        source->template_text[cursor] != '%')
        return false;
    position = cursor + 1U;
    if (position >= source->template_length)
        return false;
    if (source->template_text[position] == 'c') {
        ++position;
        if (position >= source->template_length) return false;
    }
    if (source->template_text[position] >= '0' &&
        source->template_text[position] <= '9') {
        *index = (size_t)(source->template_text[position] - '0');
        ++position;
    } else if (source->template_text[position] == '[') {
        begin = ++position;
        end = begin;
        while (end < source->template_length &&
               source->template_text[end] != ']')
            ++end;
        if (end == source->template_length || end == begin ||
            !core_inline_asm_named_input_index(
                source, source->template_text + begin,
                end - begin, index))
            return false;
        position = end + 1U;
    } else {
        return false;
    }
    if (*index >= source->input_count)
        return false;
    *consumed = position - cursor;
    return true;
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
        if (!core_inline_asm_immediate_reference(
                source, cursor, &replacement_index, &consumed) ||
            output_length > SIZE_MAX - replacement_lengths[replacement_index])
            return false;
        output_length += replacement_lengths[replacement_index];
        cursor += consumed;
    }
    *specialized_length = output_length;
    return true;
}

bool core_inline_asm_specialize_immediates(const MinicCoreLowerContext *context,
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
        if (!core_inline_asm_immediate_reference(
                source, cursor, &replacement_index, &consumed)) {
            free(specialized);
            return false;
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
bool core_inline_asm_specialize_register_output_immediates(
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
bool core_inline_asm_numeric_template(const MinicInlineAsm *source,
                                             char **template_out,
                                             size_t *template_length_out) {
    size_t cursor;
    size_t output_length;
    char *normalized;

    if (source == NULL || template_out == NULL || template_length_out == NULL ||
        source->template_text == NULL || source->template_length == SIZE_MAX ||
        source->output_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT ||
        source->input_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT -
                                  source->output_count ||
        (source->output_count != 0U && source->outputs == NULL) ||
        (source->input_count != 0U && source->inputs == NULL)) {
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
            size_t index_begin = cursor;

            operand_index = 0U;
            /* GNU %10 and %15 are single operand references, not %1 followed
             * by literal digits. Preserve every digit for the target parser. */
            do {
                operand_index = operand_index * 10U +
                    (size_t)(source->template_text[cursor] - '0');
                cursor += 1U;
                if (operand_index > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
                    free(normalized);
                    return false;
                }
            } while (cursor < source->template_length &&
                     source->template_text[cursor] >= '0' &&
                     source->template_text[cursor] <= '9');
            /* In GNU extended asm-goto, %lN refers to a C label AFTER all
             * register/memory output and input operand numbers. Preserve
             * that label reference for the target's PREL32 relocation;
             * unlike a scalar %N it does not bind a Core SSA value. */
            if (operand_index >= source->output_count + source->input_count &&
                !(source->is_goto && modifier == 'l' &&
                  source->label_count == 1U &&
                  operand_index == source->output_count + source->input_count)) {
                free(normalized);
                return false;
            }
            if (modifier != '\0') {
                normalized[output_length++] = modifier;
            }
            while (index_begin < cursor)
                normalized[output_length++] = source->template_text[index_begin++];
            continue;
        }
        if (source->template_text[cursor] == '[') {
            size_t name_begin = cursor + 1U;
            size_t name_end = name_begin;
            bool resolved;
            while (name_end < source->template_length && source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (name_end >= source->template_length || name_end == name_begin) {
                free(normalized);
                return false;
            }
            resolved = core_inline_asm_named_operand_index(
                source, source->template_text + name_begin,
                name_end - name_begin, &operand_index);
            /* GNU asm-goto label placeholders are in the *same* numeric
             * space as operands but are not scalar inputs. Resolve a named
             * %l[label] to the label slot, preserving the existing target
             * PREL32 destination policy. Never accept %l for a mismatched
             * label name or a non-goto instruction. */
            if (!resolved && modifier == 'l' && source->is_goto &&
                source->label_count == 1U && source->labels &&
                source->labels[0].name &&
                source->labels[0].name_length == name_end - name_begin &&
                memcmp(source->labels[0].name,
                       source->template_text + name_begin,
                       name_end - name_begin) == 0) {
                operand_index = source->output_count + source->input_count;
                resolved = true;
            }
            if (!resolved ||
                operand_index > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT ||
                (operand_index == MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT &&
                 !source->is_goto)) {
                free(normalized);
                return false;
            }
            if (modifier != '\0') {
                normalized[output_length++] = modifier;
            }
            if (operand_index >= 10U)
                normalized[output_length++] = (char)('0' + operand_index / 10U);
            normalized[output_length++] = (char)('0' + operand_index % 10U);
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
 bool core_inline_asm_single_label_goto_supported(
    const MinicCoreLowerContext *context, const MinicInlineAsm *source) {
    const MinicExpression *input_expression;
    const MinicInlineAsmLabel *label;
    const MinicStatement *target_statement;
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
    /* M76R_RESOLVED_IMMEDIATE_ASM_GOTO: the same single-label control-flow
       seam owns both deferred and already-resolved immediate operands. The
       lowering path below bakes a resolved operand into target text before Core
       hands the opaque template to the backend; unresolved operands retain the
       discovery-only deferred symbol contract. */
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

 bool core_inline_asm_single_label_goto_numeric_template(
    const MinicInlineAsm *source, char **template_out, size_t *template_length_out) {
    char *normalized;
    size_t cursor;
    size_t output_length;

    if (source == NULL || template_out == NULL || template_length_out == NULL ||
        source->template_text == NULL || source->template_length == SIZE_MAX ||
        source->inputs == NULL || source->input_count != 1U) {
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

 bool core_inline_asm_single_label_goto_bake_input(
    const char *template_text,
    size_t template_length,
    const char *replacement,
    size_t replacement_length,
    char **template_out,
    size_t *template_length_out) {
    char *baked;
    size_t cursor;
    size_t output_length;
    size_t output_cursor;
    size_t replacement_count;

    if (template_text == NULL || template_length == 0U || replacement == NULL ||
        replacement_length == 0U || template_out == NULL || template_length_out == NULL) {
        return false;
    }
    output_length = template_length;
    replacement_count = 0U;
    for (cursor = 0U; cursor + 1U < template_length; ++cursor) {
        /* GNU %% is a literal percent, not the beginning of operand %0.
         * Skip both bytes together: the later target emitter will turn %%
         * into one percent after immediate specialization. */
        if (template_text[cursor] == '%' && template_text[cursor + 1U] == '%') {
            cursor += 1U;
            continue;
        }
        if (template_text[cursor] == '%' && template_text[cursor + 1U] == '0') {
            if (replacement_length >= 2U) {
                size_t growth = replacement_length - 2U;
                if (output_length > SIZE_MAX - growth) {
                    return false;
                }
                output_length += growth;
            } else {
                output_length -= 2U - replacement_length;
            }
            replacement_count += 1U;
            cursor += 1U;
        }
    }
    if (replacement_count == 0U || output_length == SIZE_MAX) {
        return false;
    }
    baked = (char *)malloc(output_length + 1U);
    if (baked == NULL) {
        return false;
    }
    output_cursor = 0U;
    for (cursor = 0U; cursor < template_length; ++cursor) {
        if (template_text[cursor] == '%' && cursor + 1U < template_length &&
            template_text[cursor + 1U] == '%') {
            baked[output_cursor++] = '%';
            baked[output_cursor++] = '%';
            cursor += 1U;
            continue;
        }
        if (template_text[cursor] == '%' && cursor + 1U < template_length &&
            template_text[cursor + 1U] == '0') {
            (void)memcpy(baked + output_cursor, replacement, replacement_length);
            output_cursor += replacement_length;
            cursor += 1U;
            continue;
        }
        baked[output_cursor++] = template_text[cursor];
    }
    baked[output_cursor] = '\0';
    if (output_cursor != output_length) {
        free(baked);
        return false;
    }
    *template_out = baked;
    *template_length_out = output_length;
    return true;
}

static bool core_inline_asm_expression_is_local_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicLocalId local_id,
    unsigned int depth) {
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        depth > 8U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        return expression->value.local_id == local_id;
    }
    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ ||
        expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        return core_inline_asm_expression_is_local_depth(
            context, expression->value.unary.operand, local_id, depth + 1U);
    }
    return false;
}

 bool core_inline_asm_expression_is_local(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicLocalId local_id) {
    return core_inline_asm_expression_is_local_depth(context, expression_id, local_id, 0U);
}

