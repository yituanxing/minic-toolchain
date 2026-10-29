#ifndef MINIC_FRONTEND_TYPE_H
#define MINIC_FRONTEND_TYPE_H

#include <stdbool.h>
#include <stddef.h>

typedef size_t MinicRecordId;
typedef size_t MinicArrayTypeId;
typedef size_t MinicFunctionTypeId;
typedef size_t MinicEnumId;

#define MINIC_RECORD_INVALID ((MinicRecordId) - 1)
#define MINIC_ARRAY_TYPE_INVALID ((MinicArrayTypeId) - 1)
#define MINIC_FUNCTION_TYPE_INVALID ((MinicFunctionTypeId) - 1)
#define MINIC_ENUM_INVALID ((MinicEnumId) - 1)

/* Target-neutral function ABI identity. A non-default convention changes
 * function-pointer compatibility and direct/indirect call transport; it does
 * not prescribe any particular physical register or stack location. */
typedef enum MinicFunctionCallingConvention {
    MINIC_CALLING_CONVENTION_DEFAULT = 0,
    MINIC_CALLING_CONVENTION_MS_X64
} MinicFunctionCallingConvention;

static inline bool minic_function_calling_convention_valid(
    MinicFunctionCallingConvention convention) {
    return convention == MINIC_CALLING_CONVENTION_DEFAULT ||
           convention == MINIC_CALLING_CONVENTION_MS_X64;
}

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
    /* x86 SysV va_list is an array[1] of a 24-byte tag. For now the
     * runtime uses a pointer to a 24-byte frame-owned tag, not that array
     * representation. Preserve this distinct identity so sizeof cannot
     * silently report sizeof(void*) for __builtin_va_list. */
    MINIC_TYPE_BASE_X86_VA_LIST_CURSOR,
    /* GNU/ISO TS 18661 extended FP types are distinct C identities even
     * when they share x86 storage and argument representation with float,
     * double or long double. Do not alias these to standard C types. */
    MINIC_TYPE_BASE_FLOAT32,
    MINIC_TYPE_BASE_FLOAT64,
    MINIC_TYPE_BASE_FLOAT32X,
    MINIC_TYPE_BASE_FLOAT64X,
    /* AArch64 glibc declares GCC's NEON/SVE builtin vector types even when
     * the C program never uses SIMD. Preserve separate opaque identities:
     * typedef parsing is supported; layout and machine operations reject
     * until each vector ABI has been implemented and verified. */
    MINIC_TYPE_BASE_AARCH64_FLOAT32X4,
    MINIC_TYPE_BASE_AARCH64_FLOAT64X2,
    MINIC_TYPE_BASE_AARCH64_SVE_FLOAT32,
    MINIC_TYPE_BASE_AARCH64_SVE_FLOAT64,
    MINIC_TYPE_BASE_AARCH64_SVE_BOOL
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
    MINIC_BUILTIN_DECLARATION_ABI_CURSOR
} MinicBuiltinDeclarationRole;

MinicBuiltinDeclarationRole minic_type_builtin_declaration_role(MinicTypeBaseKind kind);

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
} MinicType;

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
/* Header-only opaque AArch64 builtin SIMD identities, not scalar aliases. */
MinicType minic_type_aarch64_header_builtin(MinicTypeBaseKind base_kind);
MinicType minic_type_x86_va_list_cursor(void);
bool minic_type_is_x86_va_list_cursor(MinicType type);
MinicType minic_type_function(MinicFunctionTypeId function_type_id);
MinicType minic_type_record(MinicRecordId record_id);
MinicType minic_type_array(MinicArrayTypeId array_type_id);
bool minic_type_add_const(MinicType type, MinicType *result);
bool minic_type_add_volatile(MinicType type, MinicType *result);
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
