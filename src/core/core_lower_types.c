#include "core/core_lower_internal.h"

const MinicDataLayout *core_data_layout(const MinicCoreLowerContext *context) {
    return context == NULL ? NULL : minic_target_info_data_layout(context->target);
}

bool core_memory_scalar_type(MinicType type) {
    /* M175B_SCALAR_DOUBLE_BRIDGE / RUNTIME_R0_FLOAT_TRANSPORT: floating
       storage/value transport is independent from admitting arithmetic.
       binary32 currently enters Core so an explicit conversion can widen it
       to binary64; no float arithmetic opcode is implied. */
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
           minic_type_is_float(type) || minic_type_is_double(type);
}

/* M152_UNSIGNED_ENUM_BIT_FIELD_OWNER: enum bit-fields keep their semantic enum
   type in AST/Core values, while C gives every complete enum a compatible
   integer type.  The existing bit-field RMW is intentionally restricted to
   unsigned storage semantics; admit an enum only when its frontend-owned
   compatible integer type is unsigned.  This preserves the signed-bit-field
   fail-closed boundary and avoids inventing enum-specific Core opcodes. */
bool core_unsigned_bit_field_semantic_type(const MinicCoreLowerContext *context,
                                                  MinicType type) {
    MinicType effective_type;

    if (!minic_type_is_integer(type)) {
        return false;
    }
    if (minic_type_is_unsigned_integer(type)) {
        return true;
    }
    return minic_type_is_enum(type) && context != NULL && context->body != NULL &&
           context->body->program != NULL &&
           minic_c0_type_effective_integer_type(
               context->body->program, type, &effective_type) &&
           minic_type_is_unsigned_integer(effective_type);
}

/* A bit-field's C value width is not necessarily the width of the memory
   allocation unit containing it. _Bool is the important case: its semantic
   integer width is one bit, while DataLayout allocates one byte. Keep the
   semantic value type for the expression result and choose an unsigned integer
   type whose object size/target width matches the storage unit used by the
   field layout. Reading storage as unsigned also gives signed bit-fields a
   well-defined logical extraction path before explicit sign extension. */
bool core_bit_field_storage_type(
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
        !minic_data_layout_type(core_data_layout(context),
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
        if (minic_data_layout_type(core_data_layout(context),
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
bool core_global_addressable_type(MinicType type) {
    return core_memory_scalar_type(type) || minic_type_is_array(type) ||
           minic_type_is_record(type);
}

bool core_scalar_expression_value_type(const MinicFunctionBodyView *body,
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
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        (expression->kind == MINIC_EXPRESSION_BINARY &&
         expression->value.binary.operator_kind == MINIC_BINARY_COMMA)) {
        /* A scalar value produced by conditional/conversion/comma evaluation
           does not carry a top-level object qualifier into Core SSA.  In
           particular, (side_effect, const_lvalue) is an rvalue whose transported
           value must match the unqualified load emitted for the right operand. */
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
