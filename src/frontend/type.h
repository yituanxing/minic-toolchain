#ifndef MINIC_FRONTEND_TYPE_H
#define MINIC_FRONTEND_TYPE_H

#include <stdbool.h>
#include <stddef.h>
#include "common/calling_convention.h"

typedef size_t MinicRecordId;
typedef size_t MinicArrayTypeId;
typedef size_t MinicFunctionTypeId;
typedef size_t MinicEnumId;

#define MINIC_RECORD_INVALID ((MinicRecordId) - 1)
#define MINIC_ARRAY_TYPE_INVALID ((MinicArrayTypeId) - 1)
#define MINIC_FUNCTION_TYPE_INVALID ((MinicFunctionTypeId) - 1)
#define MINIC_ENUM_INVALID ((MinicEnumId) - 1)

typedef enum MinicTypeBaseKind {
    MINIC_TYPE_BASE_VOID = 0,
    MINIC_TYPE_BASE_INT,
    MINIC_TYPE_BASE_ENUM,
    MINIC_TYPE_BASE_FLOAT,
    MINIC_TYPE_BASE_DOUBLE,
    MINIC_TYPE_BASE_LONG_DOUBLE,
    MINIC_TYPE_BASE_FUNCTION,
    MINIC_TYPE_BASE_RECORD,
    MINIC_TYPE_BASE_ARRAY,
    /* Genuine IEEE binary128 type, distinct from x86 80-bit long double.
     * Append to preserve existing serialized base-kind identifiers. */
    MINIC_TYPE_BASE_FLOAT128,
    /* GNU/ISO TS 18661 extended FP types are distinct C identities even
     * when they share x86 storage and argument representation with float,
     * double or long double. Do not alias these to standard C types. Keep
     * the historical numeric value 11: value 10 is retired cursor-proxy
     * source representation, not reusable for an unrelated C type. */
    MINIC_TYPE_BASE_FLOAT32 = 11,
    MINIC_TYPE_BASE_FLOAT64,
    MINIC_TYPE_BASE_FLOAT32X,
    MINIC_TYPE_BASE_FLOAT64X,
    /* GNU vector_size(N) is a target-neutral C type identity, distinct
     * from every target builtin vector. Storage does not grant SIMD ALU or
     * by-value calling convention support. Its historical numeric value 24
     * remains stable; 15..23 are retired former architecture-specific kinds. */
    MINIC_TYPE_BASE_GNU_FIXED_VECTOR = 24,
    /* All target-owned header builtin identities share one source category.
     * The selected SourcePolicy validates target_builtin_id and role. */
    MINIC_TYPE_BASE_TARGET_BUILTIN
} MinicTypeBaseKind;

/* Source-language declaration shape only: this neither approves an extension
 * for a target nor promises an executable machine value/ABI. TargetInfo owns
 * admission; this centralized compatibility adapter lets the verifier avoid
 * naming individual target ISA types. */
typedef enum MinicBuiltinDeclarationRole {
    MINIC_BUILTIN_DECLARATION_INVALID = 0,
    MINIC_BUILTIN_DECLARATION_STANDARD_FLOAT,
    MINIC_BUILTIN_DECLARATION_EXTENDED_FLOAT,
    MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
    MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE
} MinicBuiltinDeclarationRole;

typedef enum MinicIntegerSign {
    MINIC_INTEGER_SIGN_NONE = 0,
    MINIC_INTEGER_SIGN_SIGNED,
    MINIC_INTEGER_SIGN_UNSIGNED
} MinicIntegerSign;

typedef enum MinicIntegerRank {
    MINIC_INTEGER_RANK_NONE = 0,
    MINIC_INTEGER_RANK_BOOL,
    MINIC_INTEGER_RANK_CHAR,
    MINIC_INTEGER_RANK_SHORT,
    MINIC_INTEGER_RANK_INT,
    MINIC_INTEGER_RANK_LONG,
    MINIC_INTEGER_RANK_LONG_LONG,
    MINIC_INTEGER_RANK_INT128
} MinicIntegerRank;

typedef enum MinicTypeQualifier {
    MINIC_TYPE_QUALIFIER_NONE = 0,
    MINIC_TYPE_QUALIFIER_CONST = 1U << 0,
    MINIC_TYPE_QUALIFIER_VOLATILE = 1U << 1
} MinicTypeQualifier;

typedef struct MinicType {
    /* Identity, qualifiers and pointer metadata first to avoid the
       ABI alignment hole before the size_t IDs. No field changes type. */
    MinicTypeBaseKind base_kind;
    MinicIntegerSign integer_sign;
    MinicIntegerRank integer_rank;
    bool is_plain_char;
    unsigned int base_qualifiers;
    unsigned int pointer_qualifiers;
    unsigned int pointer_volatile_qualifiers;
    unsigned int pointer_depth;
    MinicRecordId record_id;
    MinicArrayTypeId array_type_id;
    MinicFunctionTypeId function_type_id;
    MinicEnumId enum_id;
    size_t explicit_alignment;
    /* For GNU fixed vectors the element's scalar base kind and integer
     * sign/rank/plain-char identity live alongside the total byte size.
     * All ordinary C types leave these two fields zeroed. */
    MinicTypeBaseKind gnu_vector_element_kind;
    size_t gnu_vector_bytes;
    /* Target-local header builtin identity. Zero/INVALID for all ordinary C
     * types; not a machine ISA request or vector calling-convention grant. */
    unsigned int target_builtin_id;
    MinicBuiltinDeclarationRole target_builtin_role;
} MinicType;

MinicBuiltinDeclarationRole minic_type_builtin_declaration_role(MinicType type);

bool minic_type_gnu_fixed_vector(MinicType element_type, size_t bytes, MinicType *result);
bool minic_type_is_gnu_fixed_vector(MinicType type);

MinicType minic_type_void(void);
MinicType minic_type_bool(void);
MinicType minic_type_char(void);
MinicType minic_type_signed_char(void);
MinicType minic_type_unsigned_char(void);
MinicType minic_type_short(void);
MinicType minic_type_unsigned_short(void);
MinicType minic_type_int(void);
MinicType minic_type_unsigned_int(void);
MinicType minic_type_long(void);
MinicType minic_type_unsigned_long(void);
MinicType minic_type_long_long(void);
MinicType minic_type_unsigned_long_long(void);
MinicType minic_type_enum(MinicEnumId enum_id);
MinicType minic_type_int128(void);
MinicType minic_type_unsigned_int128(void);
MinicType minic_type_float(void);
MinicType minic_type_double(void);
MinicType minic_type_long_double(void);
MinicType minic_type_float128(void);
MinicType minic_type_float32(void);
MinicType minic_type_float64(void);
MinicType minic_type_float32x(void);
MinicType minic_type_float64x(void);
/* Construct only registered header builtin declaration identities (opaque or
 * fixed-width storage). The selected target still admits the actual spelling
 * and type separately; this never grants machine SIMD or a vector call ABI. */
MinicType minic_type_header_builtin(unsigned int builtin_id,
                                    MinicBuiltinDeclarationRole role);
/* Source C object identity, not a machine SIMD operation or ISA permit. */
bool minic_type_is_fixed_vector128_storage(MinicType type);
/* Source C identity and CV-qualification check shared by parser and verifier.
 * Target admission of each builtin kind is deliberately the caller's duty. */
bool minic_type_fixed_vector128_binary_result(MinicType left, MinicType right,
                                            MinicType *result);
MinicType minic_type_function(MinicFunctionTypeId function_type_id);
MinicType minic_type_record(MinicRecordId record_id);
MinicType minic_type_array(MinicArrayTypeId array_type_id);
bool minic_type_add_const(MinicType type, MinicType *result);
bool minic_type_add_volatile(MinicType type, MinicType *result);
/* A struct/union member lvalue inherits both object qualifiers from its
 * containing record; its declared pointer-pointee qualifiers are unchanged. */
bool minic_type_record_member(MinicType record_type, MinicType field_type,
                              MinicType *result);
bool minic_type_unqualified(MinicType type, MinicType *result);
bool minic_type_pointer_to(MinicType pointee, MinicType *result);
bool minic_type_pointee(MinicType pointer, MinicType *result);
bool minic_type_equal(MinicType left, MinicType right);
bool minic_type_assignment_compatible(MinicType target, MinicType source);
bool minic_type_gnu_pointer_sign_compatible(MinicType target, MinicType source);
/* GNU -Wdiscarded-qualifiers: one-level object pointer, diagnosed cast only. */
bool minic_type_gnu_discard_const_one_pointer_compatible(MinicType target, MinicType source);
bool minic_type_conditional_pointer_common(MinicType left, MinicType right, MinicType *result);
bool minic_type_pointer_equality_compatible(MinicType left, MinicType right);
bool minic_type_cast_compatible(MinicType target, MinicType source);
bool minic_type_is_const(MinicType type);
bool minic_type_is_volatile(MinicType type);
bool minic_type_is_void(MinicType type);
bool minic_type_is_enum(MinicType type);
bool minic_type_is_integer(MinicType type);
bool minic_type_is_bool_integer(MinicType type);
bool minic_type_is_char_integer(MinicType type);
bool minic_type_is_plain_char(MinicType type);
bool minic_type_is_short_integer(MinicType type);
bool minic_type_is_long_integer(MinicType type);
bool minic_type_is_int128_integer(MinicType type);
bool minic_type_is_signed_integer(MinicType type);
bool minic_type_is_unsigned_integer(MinicType type);
bool minic_type_is_float(MinicType type);
bool minic_type_is_double(MinicType type);
bool minic_type_is_long_double(MinicType type);
bool minic_type_is_float128(MinicType type);
bool minic_type_is_function(MinicType type);
bool minic_type_is_record(MinicType type);
bool minic_type_is_array(MinicType type);
bool minic_type_is_pointer(MinicType type);

#endif
