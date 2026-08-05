#ifndef MINIC_FRONTEND_TYPE_H
#define MINIC_FRONTEND_TYPE_H

#include <stdbool.h>

typedef enum MinicBuiltinTypeKind {
    MINIC_BUILTIN_TYPE_INT = 0
} MinicBuiltinTypeKind;

typedef struct MinicType {
    MinicBuiltinTypeKind builtin_kind;
    unsigned int pointer_depth;
} MinicType;

MinicType minic_type_int(void);
bool minic_type_pointer_to(MinicType pointee, MinicType *result);
bool minic_type_equal(MinicType left, MinicType right);
bool minic_type_is_integer(MinicType type);
bool minic_type_is_pointer(MinicType type);

#endif
