#include "frontend/type.h"

#include <limits.h>
#include <stddef.h>

static MinicType minic_type_integer(MinicIntegerSign sign)
{
    MinicType type;

    type.base_kind = MINIC_TYPE_BASE_INT;
    type.record_id = MINIC_RECORD_INVALID;
    type.array_type_id = MINIC_ARRAY_TYPE_INVALID;
    type.integer_sign = sign;
    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    return type;
}

MinicType minic_type_void(void)
{
    MinicType type;

    type.base_kind = MINIC_TYPE_BASE_VOID;
    type.record_id = MINIC_RECORD_INVALID;
    type.array_type_id = MINIC_ARRAY_TYPE_INVALID;
    type.integer_sign = MINIC_INTEGER_SIGN_NONE;
    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    return type;
}

MinicType minic_type_int(void)
{
    return minic_type_integer(MINIC_INTEGER_SIGN_SIGNED);
}

MinicType minic_type_unsigned_int(void)
{
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED);
}

MinicType minic_type_record(MinicRecordId record_id)
{
    MinicType type;

    type.base_kind = MINIC_TYPE_BASE_RECORD;
    type.record_id = record_id;
    type.array_type_id = MINIC_ARRAY_TYPE_INVALID;
    type.integer_sign = MINIC_INTEGER_SIGN_NONE;
    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    return type;
}

MinicType minic_type_array(MinicArrayTypeId array_type_id)
{
    MinicType type;

    type.base_kind = MINIC_TYPE_BASE_ARRAY;
    type.record_id = MINIC_RECORD_INVALID;
    type.array_type_id = array_type_id;
    type.integer_sign = MINIC_INTEGER_SIGN_NONE;
    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    return type;
}

bool minic_type_add_const(MinicType type, MinicType *result)
{
    if (result == NULL) {
        return false;
    }
    *result = type;
    result->base_qualifiers |= MINIC_TYPE_QUALIFIER_CONST;
    return true;
}

bool minic_type_pointer_to(MinicType pointee, MinicType *result)
{
    if (result == NULL || pointee.pointer_depth == UINT_MAX) {
        return false;
    }
    *result = pointee;
    result->pointer_depth += 1U;
    return true;
}

bool minic_type_pointee(MinicType pointer, MinicType *result)
{
    if (result == NULL || pointer.pointer_depth == 0U) {
        return false;
    }
    *result = pointer;
    result->pointer_depth -= 1U;
    return true;
}

bool minic_type_equal(MinicType left, MinicType right)
{
    return left.base_kind == right.base_kind &&
           left.record_id == right.record_id &&
           left.array_type_id == right.array_type_id &&
           left.integer_sign == right.integer_sign &&
           left.base_qualifiers == right.base_qualifiers &&
           left.pointer_depth == right.pointer_depth;
}

bool minic_type_integer_common(
    MinicType left,
    MinicType right,
    MinicType *result)
{
    if (result == NULL ||
        !minic_type_is_integer(left) ||
        !minic_type_is_integer(right)) {
        return false;
    }
    if (minic_type_is_unsigned_integer(left) ||
        minic_type_is_unsigned_integer(right)) {
        *result = minic_type_unsigned_int();
    } else {
        *result = minic_type_int();
    }
    return true;
}

bool minic_type_assignment_compatible(MinicType target, MinicType source)
{
    if (minic_type_is_integer(target) && minic_type_is_integer(source)) {
        return true;
    }
    return minic_type_equal(target, source);
}

bool minic_type_cast_compatible(MinicType target, MinicType source)
{
    if (minic_type_is_integer(target) && minic_type_is_integer(source)) {
        return true;
    }
    return minic_type_is_pointer(target) && minic_type_is_pointer(source);
}

bool minic_type_is_const(MinicType type)
{
    return (type.base_qualifiers & MINIC_TYPE_QUALIFIER_CONST) != 0U;
}

bool minic_type_is_void(MinicType type)
{
    return type.base_kind == MINIC_TYPE_BASE_VOID &&
           type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.pointer_depth == 0U;
}

bool minic_type_is_integer(MinicType type)
{
    return type.base_kind == MINIC_TYPE_BASE_INT &&
           type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           (type.integer_sign == MINIC_INTEGER_SIGN_SIGNED ||
            type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED) &&
           type.pointer_depth == 0U;
}

bool minic_type_is_signed_integer(MinicType type)
{
    return minic_type_is_integer(type) &&
           type.integer_sign == MINIC_INTEGER_SIGN_SIGNED;
}

bool minic_type_is_unsigned_integer(MinicType type)
{
    return minic_type_is_integer(type) &&
           type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED;
}

bool minic_type_is_record(MinicType type)
{
    return type.base_kind == MINIC_TYPE_BASE_RECORD &&
           type.record_id != MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.pointer_depth == 0U;
}

bool minic_type_is_array(MinicType type)
{
    return type.base_kind == MINIC_TYPE_BASE_ARRAY &&
           type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id != MINIC_ARRAY_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.pointer_depth == 0U;
}

bool minic_type_is_pointer(MinicType type)
{
    return type.pointer_depth != 0U;
}
