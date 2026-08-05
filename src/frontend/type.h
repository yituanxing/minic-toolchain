#ifndef MINIC_FRONTEND_TYPE_H
#define MINIC_FRONTEND_TYPE_H

#include <stdbool.h>
#include <stddef.h>

typedef size_t MinicRecordId;

#define MINIC_RECORD_INVALID ((MinicRecordId)-1)

typedef enum MinicTypeBaseKind {
    MINIC_TYPE_BASE_VOID = 0,
    MINIC_TYPE_BASE_INT,
    MINIC_TYPE_BASE_RECORD
} MinicTypeBaseKind;

typedef struct MinicType {
    MinicTypeBaseKind base_kind;
    MinicRecordId record_id;
    unsigned int pointer_depth;
} MinicType;

MinicType minic_type_void(void);
MinicType minic_type_int(void);
MinicType minic_type_record(MinicRecordId record_id);
bool minic_type_pointer_to(MinicType pointee, MinicType *result);
bool minic_type_pointee(MinicType pointer, MinicType *result);
bool minic_type_equal(MinicType left, MinicType right);
bool minic_type_is_void(MinicType type);
bool minic_type_is_integer(MinicType type);
bool minic_type_is_record(MinicType type);
bool minic_type_is_pointer(MinicType type);

#endif
