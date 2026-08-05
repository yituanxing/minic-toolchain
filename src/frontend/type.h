#ifndef MINIC_FRONTEND_TYPE_H
#define MINIC_FRONTEND_TYPE_H

#include <stdbool.h>
#include <stddef.h>

typedef size_t MinicRecordId;
typedef size_t MinicArrayTypeId;

#define MINIC_RECORD_INVALID ((MinicRecordId)-1)
#define MINIC_ARRAY_TYPE_INVALID ((MinicArrayTypeId)-1)

typedef enum MinicTypeBaseKind {
    MINIC_TYPE_BASE_VOID = 0,
    MINIC_TYPE_BASE_INT,
    MINIC_TYPE_BASE_RECORD,
    MINIC_TYPE_BASE_ARRAY
} MinicTypeBaseKind;

typedef enum MinicTypeQualifier {
    MINIC_TYPE_QUALIFIER_NONE = 0,
    MINIC_TYPE_QUALIFIER_CONST = 1U << 0
} MinicTypeQualifier;

typedef struct MinicType {
    MinicTypeBaseKind base_kind;
    MinicRecordId record_id;
    MinicArrayTypeId array_type_id;
    unsigned int base_qualifiers;
    unsigned int pointer_depth;
} MinicType;

MinicType minic_type_void(void);
MinicType minic_type_int(void);
MinicType minic_type_record(MinicRecordId record_id);
MinicType minic_type_array(MinicArrayTypeId array_type_id);
bool minic_type_add_const(MinicType type, MinicType *result);
bool minic_type_pointer_to(MinicType pointee, MinicType *result);
bool minic_type_pointee(MinicType pointer, MinicType *result);
bool minic_type_equal(MinicType left, MinicType right);
bool minic_type_is_const(MinicType type);
bool minic_type_is_void(MinicType type);
bool minic_type_is_integer(MinicType type);
bool minic_type_is_record(MinicType type);
bool minic_type_is_array(MinicType type);
bool minic_type_is_pointer(MinicType type);

#endif
