#include "frontend/type.h"

#include <limits.h>
#include <stddef.h>

MinicType minic_type_void(void)
{
    MinicType type;

    type.base_kind = MINIC_TYPE_BASE_VOID;
    type.record_id = MINIC_RECORD_INVALID;
    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    return type;
}

MinicType minic_type_int(void)
{
    MinicType type;

    type.base_kind = MINIC_TYPE_BASE_INT;
    type.record_id = MINIC_RECORD_INVALID;
    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    return type;
}

MinicType minic_type_record(MinicRecordId record_id)
{
    MinicType type;

    type.base_kind = MINIC_TYPE_BASE_RECORD;
    type.record_id = record_id;
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
           left.base_qualifiers == right.base_qualifiers &&
           left.pointer_depth == right.pointer_depth;
}

bool minic_type_is_const(MinicType type)
{
    return (type.base_qualifiers & MINIC_TYPE_QUALIFIER_CONST) != 0U;
}

bool minic_type_is_void(MinicType type)
{
    return type.base_kind == MINIC_TYPE_BASE_VOID &&
           type.record_id == MINIC_RECORD_INVALID &&
           type.pointer_depth == 0U;
}

bool minic_type_is_integer(MinicType type)
{
    return type.base_kind == MINIC_TYPE_BASE_INT &&
           type.record_id == MINIC_RECORD_INVALID &&
           type.pointer_depth == 0U;
}

bool minic_type_is_record(MinicType type)
{
    return type.base_kind == MINIC_TYPE_BASE_RECORD &&
           type.record_id != MINIC_RECORD_INVALID &&
           type.pointer_depth == 0U;
}

bool minic_type_is_pointer(MinicType type)
{
    return type.pointer_depth != 0U;
}
