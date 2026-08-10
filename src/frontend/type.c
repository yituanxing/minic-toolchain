#include "frontend/type.h"

#include <limits.h>
#include <stddef.h>

static unsigned int minic_type_pointer_qualifier_capacity(void) {
    return (unsigned int)(sizeof(unsigned int) * CHAR_BIT);
}

static bool minic_type_pointer_qualifiers_are_valid(MinicType type) {
    unsigned int capacity;

    capacity = minic_type_pointer_qualifier_capacity();
    if (type.pointer_depth > capacity) {
        return false;
    }
    if (type.pointer_depth == capacity) {
        return true;
    }
    return (type.pointer_qualifiers >> type.pointer_depth) == 0U &&
           (type.pointer_volatile_qualifiers >> type.pointer_depth) == 0U;
}

static MinicType minic_type_integer(MinicIntegerSign sign, MinicIntegerRank rank) {
    MinicType type;

    type.base_kind = MINIC_TYPE_BASE_INT;
    type.record_id = MINIC_RECORD_INVALID;
    type.array_type_id = MINIC_ARRAY_TYPE_INVALID;
    type.function_type_id = MINIC_FUNCTION_TYPE_INVALID;
    type.integer_sign = sign;
    type.integer_rank = rank;
    type.is_plain_char = false;
    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_volatile_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    type.explicit_alignment = 0U;
    return type;
}

static MinicType minic_type_scalar(MinicTypeBaseKind base_kind) {
    MinicType type;

    type.base_kind = base_kind;
    type.record_id = MINIC_RECORD_INVALID;
    type.array_type_id = MINIC_ARRAY_TYPE_INVALID;
    type.function_type_id = MINIC_FUNCTION_TYPE_INVALID;
    type.integer_sign = MINIC_INTEGER_SIGN_NONE;
    type.integer_rank = MINIC_INTEGER_RANK_NONE;
    type.is_plain_char = false;
    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_volatile_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    type.explicit_alignment = 0U;
    return type;
}

static bool minic_type_same_unqualified_identity(MinicType left, MinicType right) {
    return left.base_kind == right.base_kind && left.record_id == right.record_id &&
           left.array_type_id == right.array_type_id &&
           left.function_type_id == right.function_type_id &&
           left.integer_sign == right.integer_sign && left.integer_rank == right.integer_rank &&
           left.is_plain_char == right.is_plain_char && left.pointer_depth == right.pointer_depth;
}

static bool minic_type_pointer_qualification_compatible(MinicType target, MinicType source) {
    MinicType unqualified_target;
    MinicType unqualified_source;

    if (!minic_type_unqualified(target, &unqualified_target) ||
        !minic_type_unqualified(source, &unqualified_source) ||
        unqualified_target.pointer_depth != 1U || unqualified_source.pointer_depth != 1U ||
        !minic_type_same_unqualified_identity(unqualified_target, unqualified_source)) {
        return false;
    }
    return (unqualified_source.base_qualifiers & ~unqualified_target.base_qualifiers) == 0U;
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

MinicType minic_type_bool(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_BOOL);
}

MinicType minic_type_char(void) {
    MinicType type;

    type = minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_CHAR);
    type.is_plain_char = true;
    return type;
}

MinicType minic_type_signed_char(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_SIGNED, MINIC_INTEGER_RANK_CHAR);
}

MinicType minic_type_unsigned_char(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_CHAR);
}

MinicType minic_type_short(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_SIGNED, MINIC_INTEGER_RANK_SHORT);
}

MinicType minic_type_unsigned_short(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_SHORT);
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

MinicType minic_type_long_long(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_SIGNED, MINIC_INTEGER_RANK_LONG_LONG);
}

MinicType minic_type_unsigned_long_long(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_LONG_LONG);
}

MinicType minic_type_int128(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_SIGNED, MINIC_INTEGER_RANK_INT128);
}

MinicType minic_type_unsigned_int128(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_INT128);
}

MinicType minic_type_float(void) {
    return minic_type_scalar(MINIC_TYPE_BASE_FLOAT);
}

MinicType minic_type_double(void) {
    return minic_type_scalar(MINIC_TYPE_BASE_DOUBLE);
}

MinicType minic_type_function(MinicFunctionTypeId function_type_id) {
    MinicType type;

    type = minic_type_scalar(MINIC_TYPE_BASE_FUNCTION);
    type.function_type_id = function_type_id;
    return type;
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
    if (result == NULL || !minic_type_pointer_qualifiers_are_valid(type) ||
        minic_type_is_function(type)) {
        return false;
    }
    *result = type;
    if (type.pointer_depth == 0U) {
        result->base_qualifiers |= MINIC_TYPE_QUALIFIER_CONST;
    } else {
        result->pointer_qualifiers |= 1U << (type.pointer_depth - 1U);
    }
    return true;
}

bool minic_type_add_volatile(MinicType type, MinicType *result) {
    if (result == NULL || !minic_type_pointer_qualifiers_are_valid(type) ||
        minic_type_is_function(type)) {
        return false;
    }
    *result = type;
    if (type.pointer_depth == 0U) {
        result->base_qualifiers |= MINIC_TYPE_QUALIFIER_VOLATILE;
    } else {
        result->pointer_volatile_qualifiers |= 1U << (type.pointer_depth - 1U);
    }
    return true;
}

bool minic_type_unqualified(MinicType type, MinicType *result) {
    if (result == NULL || !minic_type_pointer_qualifiers_are_valid(type)) {
        return false;
    }
    *result = type;
    if (type.pointer_depth == 0U) {
        result->base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    } else {
        result->pointer_qualifiers &= ~(1U << (type.pointer_depth - 1U));
        result->pointer_volatile_qualifiers &= ~(1U << (type.pointer_depth - 1U));
    }
    return true;
}

bool minic_type_pointer_to(MinicType pointee, MinicType *result) {
    if (result == NULL || !minic_type_pointer_qualifiers_are_valid(pointee) ||
        pointee.pointer_depth >= minic_type_pointer_qualifier_capacity()) {
        return false;
    }
    *result = pointee;
    result->pointer_depth += 1U;
    return true;
}

bool minic_type_pointee(MinicType pointer, MinicType *result) {
    unsigned int removed_level;

    if (result == NULL || pointer.pointer_depth == 0U ||
        !minic_type_pointer_qualifiers_are_valid(pointer)) {
        return false;
    }
    *result = pointer;
    removed_level = pointer.pointer_depth - 1U;
    result->pointer_qualifiers &= ~(1U << removed_level);
    result->pointer_volatile_qualifiers &= ~(1U << removed_level);
    result->pointer_depth -= 1U;
    return true;
}

bool minic_type_equal(MinicType left, MinicType right) {
    return minic_type_same_unqualified_identity(left, right) &&
           left.base_qualifiers == right.base_qualifiers &&
           left.pointer_qualifiers == right.pointer_qualifiers &&
           left.pointer_volatile_qualifiers == right.pointer_volatile_qualifiers;
}

bool minic_type_integer_promotion(MinicType type, MinicType *result) {
    if (result == NULL || !minic_type_is_integer(type)) {
        return false;
    }
    if (type.integer_rank == MINIC_INTEGER_RANK_BOOL ||
        type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
        type.integer_rank == MINIC_INTEGER_RANK_SHORT) {
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
    if (type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG) {
        *result = minic_type_is_unsigned_integer(type) ? minic_type_unsigned_long_long()
                                                       : minic_type_long_long();
        return true;
    }
    if (type.integer_rank == MINIC_INTEGER_RANK_INT128) {
        *result = minic_type_is_unsigned_integer(type) ? minic_type_unsigned_int128()
                                                       : minic_type_int128();
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

    /* On the active RV64 data model, signed long and signed long long both
       represent every value of unsigned int. Signed __int128 additionally
       represents every value of the narrower unsigned integer ranks. */
    if ((signed_type.integer_rank >= MINIC_INTEGER_RANK_LONG &&
         unsigned_type.integer_rank <= MINIC_INTEGER_RANK_INT) ||
        (signed_type.integer_rank == MINIC_INTEGER_RANK_INT128 &&
         unsigned_type.integer_rank <= MINIC_INTEGER_RANK_LONG_LONG)) {
        *result = signed_type;
        return true;
    }

    *result = minic_type_integer_with_rank(MINIC_INTEGER_SIGN_UNSIGNED, signed_type.integer_rank);
    return true;
}

static bool minic_type_void_object_pointer_compatible(MinicType target, MinicType source) {
    MinicType target_pointee;
    MinicType source_pointee;
    bool target_is_void;
    bool source_is_void;

    if (!minic_type_is_pointer(target) || !minic_type_is_pointer(source) ||
        !minic_type_pointee(target, &target_pointee) ||
        !minic_type_pointee(source, &source_pointee)) {
        return false;
    }
    target_is_void = target.pointer_depth == 1U && minic_type_is_void(target_pointee);
    source_is_void = source.pointer_depth == 1U && minic_type_is_void(source_pointee);
    if (target_is_void == source_is_void) {
        return false;
    }
    if ((target_is_void && minic_type_is_function(source_pointee)) ||
        (source_is_void && minic_type_is_function(target_pointee))) {
        return false;
    }
    /* A pointer object is itself an object, so T ** is compatible with void *.
       Preserve the existing qualifier check for the one-level object case. */
    if ((target_is_void && source.pointer_depth > 1U) ||
        (source_is_void && target.pointer_depth > 1U)) {
        return true;
    }
    return (source_pointee.base_qualifiers & ~target_pointee.base_qualifiers) == 0U;
}

bool minic_type_assignment_compatible(MinicType target, MinicType source) {
    MinicType unqualified_target;
    MinicType unqualified_source;

    if ((minic_type_is_integer(target) && minic_type_is_integer(source)) ||
        (minic_type_is_float(target) && minic_type_is_float(source)) ||
        (minic_type_is_double(target) && minic_type_is_double(source))) {
        return true;
    }
    if (!minic_type_unqualified(target, &unqualified_target) ||
        !minic_type_unqualified(source, &unqualified_source)) {
        return false;
    }
    return minic_type_equal(unqualified_target, unqualified_source) ||
           minic_type_pointer_qualification_compatible(unqualified_target, unqualified_source) ||
           minic_type_void_object_pointer_compatible(unqualified_target, unqualified_source);
}

bool minic_type_conditional_pointer_common(MinicType left, MinicType right, MinicType *result) {
    MinicType left_pointer;
    MinicType right_pointer;
    MinicType left_pointee;
    MinicType right_pointee;
    MinicType left_unqualified;
    MinicType right_unqualified;
    MinicType composite_pointee;
    bool merge_const;
    bool merge_volatile;

    if (result == NULL || !minic_type_is_pointer(left) || !minic_type_is_pointer(right) ||
        !minic_type_unqualified(left, &left_pointer) ||
        !minic_type_unqualified(right, &right_pointer) ||
        !minic_type_pointee(left_pointer, &left_pointee) ||
        !minic_type_pointee(right_pointer, &right_pointee) ||
        !minic_type_unqualified(left_pointee, &left_unqualified) ||
        !minic_type_unqualified(right_pointee, &right_unqualified)) {
        return false;
    }

    merge_const = minic_type_is_const(left_pointee) || minic_type_is_const(right_pointee);
    merge_volatile = minic_type_is_volatile(left_pointee) || minic_type_is_volatile(right_pointee);

    if (minic_type_equal(left_unqualified, right_unqualified)) {
        composite_pointee = left_unqualified;
    } else if (minic_type_is_void(left_unqualified) && !minic_type_is_function(right_unqualified)) {
        composite_pointee = minic_type_void();
    } else if (minic_type_is_void(right_unqualified) && !minic_type_is_function(left_unqualified)) {
        composite_pointee = minic_type_void();
    } else {
        return false;
    }

    if (merge_const && !minic_type_add_const(composite_pointee, &composite_pointee)) {
        return false;
    }
    if (merge_volatile && !minic_type_add_volatile(composite_pointee, &composite_pointee)) {
        return false;
    }
    return minic_type_pointer_to(composite_pointee, result);
}

bool minic_type_pointer_equality_compatible(MinicType left, MinicType right) {
    return minic_type_is_pointer(left) && minic_type_is_pointer(right) &&
           (minic_type_assignment_compatible(left, right) ||
            minic_type_assignment_compatible(right, left));
}

bool minic_type_cast_compatible(MinicType target, MinicType source) {
    if (minic_type_is_integer(target) &&
        (minic_type_is_integer(source) || minic_type_is_double(source))) {
        return true;
    }
    if (minic_type_is_double(target) &&
        (minic_type_is_integer(source) || minic_type_is_float(source))) {
        return true;
    }
    if ((minic_type_is_pointer(target) && minic_type_is_integer(source)) ||
        (minic_type_is_integer(target) && minic_type_is_pointer(source))) {
        return true;
    }
    return minic_type_is_pointer(target) && minic_type_is_pointer(source);
}

bool minic_type_is_const(MinicType type) {
    if (!minic_type_pointer_qualifiers_are_valid(type)) {
        return false;
    }
    if (type.pointer_depth == 0U) {
        return (type.base_qualifiers & MINIC_TYPE_QUALIFIER_CONST) != 0U;
    }
    return (type.pointer_qualifiers & (1U << (type.pointer_depth - 1U))) != 0U;
}

bool minic_type_is_volatile(MinicType type) {
    if (!minic_type_pointer_qualifiers_are_valid(type)) {
        return false;
    }
    if (type.pointer_depth == 0U) {
        return (type.base_qualifiers & MINIC_TYPE_QUALIFIER_VOLATILE) != 0U;
    }
    return (type.pointer_volatile_qualifiers & (1U << (type.pointer_depth - 1U))) != 0U;
}

bool minic_type_is_void(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_VOID && type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.integer_rank == MINIC_INTEGER_RANK_NONE && !type.is_plain_char &&
           type.pointer_qualifiers == MINIC_TYPE_QUALIFIER_NONE && type.pointer_depth == 0U;
}

bool minic_type_is_integer(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_INT && type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
           (type.integer_sign == MINIC_INTEGER_SIGN_SIGNED ||
            type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED) &&
           (type.integer_rank == MINIC_INTEGER_RANK_BOOL ||
            type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
            type.integer_rank == MINIC_INTEGER_RANK_SHORT ||
            type.integer_rank == MINIC_INTEGER_RANK_INT ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG ||
            type.integer_rank == MINIC_INTEGER_RANK_INT128) &&
           minic_type_plain_char_identity_is_valid(type) &&
           type.pointer_qualifiers == MINIC_TYPE_QUALIFIER_NONE && type.pointer_depth == 0U;
}

bool minic_type_is_bool_integer(MinicType type) {
    return minic_type_is_integer(type) && type.integer_rank == MINIC_INTEGER_RANK_BOOL;
}

bool minic_type_is_char_integer(MinicType type) {
    return minic_type_is_integer(type) && type.integer_rank == MINIC_INTEGER_RANK_CHAR;
}

bool minic_type_is_plain_char(MinicType type) {
    return minic_type_is_char_integer(type) && type.is_plain_char;
}

bool minic_type_is_short_integer(MinicType type) {
    return minic_type_is_integer(type) && type.integer_rank == MINIC_INTEGER_RANK_SHORT;
}

bool minic_type_is_long_integer(MinicType type) {
    return minic_type_is_integer(type) && (type.integer_rank == MINIC_INTEGER_RANK_LONG ||
                                           type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG);
}

bool minic_type_is_int128_integer(MinicType type) {
    return minic_type_is_integer(type) && type.integer_rank == MINIC_INTEGER_RANK_INT128;
}

bool minic_type_is_signed_integer(MinicType type) {
    return minic_type_is_integer(type) && type.integer_sign == MINIC_INTEGER_SIGN_SIGNED;
}

bool minic_type_is_unsigned_integer(MinicType type) {
    return minic_type_is_integer(type) && type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED;
}

bool minic_type_is_float(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_FLOAT && type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.integer_rank == MINIC_INTEGER_RANK_NONE && !type.is_plain_char &&
           type.pointer_qualifiers == MINIC_TYPE_QUALIFIER_NONE && type.pointer_depth == 0U;
}

bool minic_type_is_double(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_DOUBLE && type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.integer_rank == MINIC_INTEGER_RANK_NONE && !type.is_plain_char &&
           type.pointer_qualifiers == MINIC_TYPE_QUALIFIER_NONE && type.pointer_depth == 0U;
}

bool minic_type_is_function(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_FUNCTION && type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.function_type_id != MINIC_FUNCTION_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.integer_rank == MINIC_INTEGER_RANK_NONE && !type.is_plain_char &&
           type.base_qualifiers == MINIC_TYPE_QUALIFIER_NONE &&
           type.pointer_qualifiers == MINIC_TYPE_QUALIFIER_NONE && type.pointer_depth == 0U;
}

bool minic_type_is_record(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_RECORD && type.record_id != MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.integer_rank == MINIC_INTEGER_RANK_NONE && !type.is_plain_char &&
           type.pointer_qualifiers == MINIC_TYPE_QUALIFIER_NONE && type.pointer_depth == 0U;
}

bool minic_type_is_array(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_ARRAY && type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id != MINIC_ARRAY_TYPE_INVALID &&
           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.integer_rank == MINIC_INTEGER_RANK_NONE && !type.is_plain_char &&
           type.pointer_qualifiers == MINIC_TYPE_QUALIFIER_NONE && type.pointer_depth == 0U;
}

bool minic_type_is_pointer(MinicType type) {
    return type.pointer_depth != 0U && minic_type_pointer_qualifiers_are_valid(type);
}
