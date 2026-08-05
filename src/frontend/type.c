#include "frontend/type.h"

#include <limits.h>
#include <stddef.h>

MinicType minic_type_int(void)
{
    MinicType type;

    type.builtin_kind = MINIC_BUILTIN_TYPE_INT;
    type.pointer_depth = 0U;
    return type;
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
    return left.builtin_kind == right.builtin_kind &&
           left.pointer_depth == right.pointer_depth;
}

bool minic_type_is_integer(MinicType type)
{
    return type.builtin_kind == MINIC_BUILTIN_TYPE_INT &&
           type.pointer_depth == 0U;
}

bool minic_type_is_pointer(MinicType type)
{
    return type.pointer_depth != 0U;
}
