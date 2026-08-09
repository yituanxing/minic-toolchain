#ifndef MINIC_FRONTEND_TYPE_H
#define MINIC_FRONTEND_TYPE_H

#include <stdbool.h>
#include <stddef.h>

typedef size_t MinicRecordId;
typedef size_t MinicArrayTypeId;
typedef size_t MinicFunctionTypeId;

#define MINIC_RECORD_INVALID ((MinicRecordId) - 1)
#define MINIC_ARRAY_TYPE_INVALID ((MinicArrayTypeId) - 1)
#define MINIC_FUNCTION_TYPE_INVALID ((MinicFunctionTypeId) - 1)

typedef enum MinicTypeBaseKind {
    MINIC_TYPE_BASE_VOID = 0,
    MINIC_TYPE_BASE_INT,
    MINIC_TYPE_BASE_FLOAT,
    MINIC_TYPE_BASE_DOUBLE,
    MINIC_TYPE_BASE_FUNCTION,
    MINIC_TYPE_BASE_RECORD,
    MINIC_TYPE_BASE_ARRAY
} MinicTypeBaseKind;

typedef enum MinicIntegerSign {
    MINIC_INTEGER_SIGN_NONE = 0,
    MINIC_INTEGER_SIGN_SIGNED,
    MINIC_INTEGER_SIGN_UNSIGNED
} MinicIntegerSign;

typedef enum MinicIntegerRank {
    MINIC_INTEGER_RANK_NONE = 0,
    MINIC_INTEGER_RANK_CHAR,
    MINIC_INTEGER_RANK_SHORT,
    MINIC_INTEGER_RANK_INT,
    MINIC_INTEGER_RANK_LONG
} MinicIntegerRank;

typedef enum MinicTypeQualifier {
    MINIC_TYPE_QUALIFIER_NONE = 0,
    MINIC_TYPE_QUALIFIER_CONST = 1U << 0
} MinicTypeQualifier;

typedef struct MinicType {
    MinicTypeBaseKind base_kind;
    MinicRecordId record_id;
    MinicArrayTypeId array_type_id;
    MinicFunctionTypeId function_type_id;
    MinicIntegerSign integer_sign;
    MinicIntegerRank integer_rank;
    bool is_plain_char;
    unsigned int base_qualifiers;
    unsigned int pointer_qualifiers;
    unsigned int pointer_depth;
} MinicType;

MinicType minic_type_void(void);
MinicType minic_type_char(void);
MinicType minic_type_unsigned_char(void);
MinicType minic_type_short(void);
MinicType minic_type_unsigned_short(void);
MinicType minic_type_int(void);
MinicType minic_type_unsigned_int(void);
MinicType minic_type_long(void);
MinicType minic_type_unsigned_long(void);
MinicType minic_type_float(void);
MinicType minic_type_double(void);
MinicType minic_type_function(MinicFunctionTypeId function_type_id);
MinicType minic_type_record(MinicRecordId record_id);
MinicType minic_type_array(MinicArrayTypeId array_type_id);
bool minic_type_add_const(MinicType type, MinicType *result);
bool minic_type_unqualified(MinicType type, MinicType *result);
bool minic_type_pointer_to(MinicType pointee, MinicType *result);
bool minic_type_pointee(MinicType pointer, MinicType *result);
bool minic_type_equal(MinicType left, MinicType right);
bool minic_type_integer_promotion(MinicType type, MinicType *result);
bool minic_type_integer_common(MinicType left, MinicType right, MinicType *result);
bool minic_type_assignment_compatible(MinicType target, MinicType source);
bool minic_type_pointer_equality_compatible(MinicType left, MinicType right);
bool minic_type_cast_compatible(MinicType target, MinicType source);
bool minic_type_is_const(MinicType type);
bool minic_type_is_void(MinicType type);
bool minic_type_is_integer(MinicType type);
bool minic_type_is_char_integer(MinicType type);
bool minic_type_is_plain_char(MinicType type);
bool minic_type_is_short_integer(MinicType type);
bool minic_type_is_long_integer(MinicType type);
bool minic_type_is_signed_integer(MinicType type);
bool minic_type_is_unsigned_integer(MinicType type);
bool minic_type_is_float(MinicType type);
bool minic_type_is_double(MinicType type);
bool minic_type_is_function(MinicType type);
bool minic_type_is_record(MinicType type);
bool minic_type_is_array(MinicType type);
bool minic_type_is_pointer(MinicType type);

#endif
