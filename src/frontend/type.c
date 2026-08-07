#include "frontend/type.h"

#include <limits.h>
#include <stddef.h>

static MinicType minic_type_integer(MinicIntegerSign sign, MinicIntegerRank rank) {
    MinicType type;

    type.base_kind = MINIC_TYPE_BASE_INT;
    type.record_id = MINIC_RECORD_INVALID;
    type.array_type_id = MINIC_ARRAY_TYPE_INVALID;
    type.integer_sign = sign;
    type.integer_rank = rank;
    type.is_plain_char = false;
    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    return type;
}

static MinicType minic_type_scalar(MinicTypeBaseKind base_kind) {
    MinicType type;

    type.base_kind = base_kind;
    type.record_id = MINIC_RECORD_INVALID;
    type.array_type_id = MINIC_ARRAY_TYPE_INVALID;
    type.integer_sign = MINIC_INTEGER_SIGN_NONE;
    type.integer_rank = MINIC_INTEGER_RANK_NONE;
    type.is_plain_char = false;
    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    return type;
}

static bool minic_type_same_unqualified_identity(MinicType left, MinicType right) {
    return left.base_kind == right.base_kind && left.record_id == right.record_id &&
           left.array_type_id == right.array_type_id && left.integer_sign == right.integer_sign &&
           left.integer_rank == right.integer_rank && left.is_plain_char == right.is_plain_char &&
           left.pointer_depth == right.pointer_depth;
}

static bool minic_type_pointer_qualification_compatible(MinicType target, MinicType source) {
    if (target.pointer_depth != 1U || source.pointer_depth != 1U ||
        !minic_type_same_unqualified_identity(target, source)) {
        return false;
    }
    return (source.base_qualifiers & ~target.base_qualifiers) == 0U;
}

static bool minic_type_plain_char_identity_is_valid(MinicType type) {
    if (!type.is_plain_char) {
        return true;
    }
    if (type.integer_sign != MINIC_INTEGER_SIGN_UNSIGNED) {
        return false;
    }
    return type.integer_rank == MINIC_INTEGER_RANK_CHAR;
}

MinicType minic_type_void(void) {
    return minic_type_scalar(MINIC_TYPE_BASE_VOID);
}

MinicType minic_type_char(void) {
    MinicType type;

    type = minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_CHAR);
    type.is_plain_char = true;
    return type;
}

MinicType minic_type_unsigned_char(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_CHAR);
}

MinicType minic_type_int(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_SIGNED, MINIC_INTEGER_RANK_INT);
}

MinicType minic_type_unsigned_int(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_INT);
}

MinicType minic_type_long(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_SIGNED, MINIC_INTEGER_RANK_LONG);
}

MinicType minic_type_unsigned_long(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_LONG);
}

MinicType minic_type_double(void) {
    return minic_type_scalar(MINIC_TYPE_BASE_DOUBLE);
}

MinicType minic_type_record(MinicRecordId record_id) {
    MinicType type;

    type = minic_type_scalar(MINIC_TYPE_BASE_RECORD);
    type.record_id = record_id;
    return type;
}

MinicType minic_type_array(MinicArrayTypeId array_type_id) {
    MinicType type;

    type = minic_type_scalar(MINIC_TYPE_BASE_ARRAY);
    type.array_type_id = array_type_id;
    return type;
}

bool minic_type_add_const(MinicType type, MinicType *result) {
    if (result == NULL) {
        return false;
    }
    *result = type;
    result->base_qualifiers |= MINIC_TYPE_QUALIFIER_CONST;
    return true;
}

bool minic_type_pointer_to(MinicType pointee, MinicType *result) {
    if (result == NULL || pointee.pointer_depth == UINT_MAX) {
        return false;
    }
    *result = pointee;
    result->pointer_depth += 1U;
    return true;
}

bool minic_type_pointee(MinicType pointer, MinicType *result) {
    if (result == NULL || pointer.pointer_depth == 0U) {
        return false;
    }
    *result = pointer;
    result->pointer_depth -= 1U;
    return true;
}

bool minic_type_equal(MinicType left, MinicType right) {
    return minic_type_same_unqualified_identity(left, right) &&
           left.base_qualifiers == right.base_qualifiers;
}

bool minic_type_integer_promotion(MinicType type, MinicType *result) {
    if (result == NULL || !minic_type_is_integer(type)) {
        return false;
    }
    if (type.integer_rank == MINIC_INTEGER_RANK_CHAR) {
        *result = minic_type_int();
        return true;
    }
    if (type.integer_rank == MINIC_INTEGER_RANK_INT) {
        *result =
            minic_type_is_unsigned_integer(type) ? minic_type_unsigned_int() : minic_type_int();
        return true;
    }
    if (type.integer_rank == MINIC_INTEGER_RANK_LONG) {
        *result =
            minic_type_is_unsigned_integer(type) ? minic_type_unsigned_long() : minic_type_long();
        return true;
    }
    return false;
}

static MinicType minic_type_integer_with_rank(MinicIntegerSign sign, MinicIntegerRank rank) {
    return minic_type_integer(sign, rank);
}

bool minic_type_integer_common(MinicType left, MinicType right, MinicType *result) {
    MinicType promoted_left;
    MinicType promoted_right;
    MinicType signed_type;
    MinicType unsigned_type;

    if (result == NULL || !minic_type_integer_promotion(left, &promoted_left) ||
        !minic_type_integer_promotion(right, &promoted_right)) {
        return false;
    }
    if (minic_type_equal(promoted_left, promoted_right)) {
        *result = promoted_left;
        return true;
    }
    if (promoted_left.integer_sign == promoted_right.integer_sign) {
        *result = promoted_left.integer_rank > promoted_right.integer_rank ? promoted_left
                                                                           : promoted_right;
        return true;
    }

    signed_type = minic_type_is_signed_integer(promoted_left) ? promoted_left : promoted_right;
    unsigned_type = minic_type_is_unsigned_integer(promoted_left) ? promoted_left : promoted_right;
    if (unsigned_type.integer_rank >= signed_type.integer_rank) {
        *result =
            minic_type_integer_with_rank(MINIC_INTEGER_SIGN_UNSIGNED, unsigned_type.integer_rank);
        return true;
    }

    /* On the active RV64 data model, signed long represents every unsigned int value. */
    if (signed_type.integer_rank == MINIC_INTEGER_RANK_LONG &&
        unsigned_type.integer_rank == MINIC_INTEGER_RANK_INT) {
        *result = signed_type;
        return true;
    }

    *result = minic_type_integer_with_rank(MINIC_INTEGER_SIGN_UNSIGNED, signed_type.integer_rank);
    return true;
}

bool minic_type_assignment_compatible(MinicType target, MinicType source) {
    if ((minic_type_is_integer(target) && minic_type_is_integer(source)) ||
        (minic_type_is_double(target) && minic_type_is_double(source))) {
        return true;
    }
    return minic_type_equal(target, source) ||
           minic_type_pointer_qualification_compatible(target, source);
}

bool minic_type_cast_compatible(MinicType target, MinicType source) {
    if (minic_type_is_integer(target) && minic_type_is_integer(source)) {
        return true;
    }
    return minic_type_is_pointer(target) && minic_type_is_pointer(source);
}

bool minic_type_is_const(MinicType type) {
    return (type.base_qualifiers & MINIC_TYPE_QUALIFIER_CONST) != 0U;
}

bool minic_type_is_void(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_VOID && type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.integer_rank == MINIC_INTEGER_RANK_NONE && !type.is_plain_char &&
           type.pointer_depth == 0U;
}

bool minic_type_is_integer(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_INT && type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           (type.integer_sign == MINIC_INTEGER_SIGN_SIGNED ||
            type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED) &&
           (type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
            type.integer_rank == MINIC_INTEGER_RANK_INT ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG) &&
           minic_type_plain_char_identity_is_valid(type) && type.pointer_depth == 0U;
}

bool minic_type_is_char_integer(MinicType type) {
    return minic_type_is_integer(type) && type.integer_rank == MINIC_INTEGER_RANK_CHAR;
}

bool minic_type_is_plain_char(MinicType type) {
    return minic_type_is_char_integer(type) && type.is_plain_char;
}

bool minic_type_is_long_integer(MinicType type) {
    return minic_type_is_integer(type) && type.integer_rank == MINIC_INTEGER_RANK_LONG;
}

bool minic_type_is_signed_integer(MinicType type) {
    return minic_type_is_integer(type) && type.integer_sign == MINIC_INTEGER_SIGN_SIGNED;
}

bool minic_type_is_unsigned_integer(MinicType type) {
    return minic_type_is_integer(type) && type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED;
}

bool minic_type_is_double(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_DOUBLE && type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.integer_rank == MINIC_INTEGER_RANK_NONE && !type.is_plain_char &&
           type.pointer_depth == 0U;
}

bool minic_type_is_record(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_RECORD && type.record_id != MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.integer_rank == MINIC_INTEGER_RANK_NONE && !type.is_plain_char &&
           type.pointer_depth == 0U;
}

bool minic_type_is_array(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_ARRAY && type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id != MINIC_ARRAY_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.integer_rank == MINIC_INTEGER_RANK_NONE && !type.is_plain_char &&
           type.pointer_depth == 0U;
}

bool minic_type_is_pointer(MinicType type) {
    return type.pointer_depth != 0U;
}
