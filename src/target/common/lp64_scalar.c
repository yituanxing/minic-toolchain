#include "target/common/lp64_scalar.h"
#include "target/common/lp64_aggregate.h"

/* Shared exact LP64 scalar gates from accepted target emitters. */

bool minic_lp64_core_kind(const MinicCoreTypeContext *context,
                          MinicCoreTypeId id, MinicCoreTypeKind kind) {
    const MinicCoreType *type;
    id = minic_core_type_unqualified(context, id);
    type = minic_core_type_get(context, id);
    return type != NULL && type->kind == kind;
}

bool minic_lp64_core_pointer_pointee(const MinicCoreTypeContext *context,
                                     MinicCoreTypeId id,
                                     MinicCoreTypeId *pointee) {
    const MinicCoreType *type;
    if (pointee == NULL) return false;
    id = minic_core_type_unqualified(context, id);
    type = minic_core_type_get(context, id);
    if (type == NULL || type->kind != MINIC_CORE_TYPE_POINTER ||
        minic_core_type_get(context, type->value.pointer.pointee) == NULL)
        return false;
    *pointee = type->value.pointer.pointee;
    return true;
}

bool minic_lp64_core_equal_unqualified(const MinicCoreTypeContext *context,
                                       MinicCoreTypeId left,
                                       MinicCoreTypeId right) {
    const MinicCoreTypeId lhs = minic_core_type_unqualified(context, left);
    const MinicCoreTypeId rhs = minic_core_type_unqualified(context, right);
    return lhs != MINIC_CORE_TYPE_INVALID && lhs == rhs;
}

bool minic_lp64_core_integer_width(const MinicCoreTypeContext *context,
                                   MinicCoreTypeId id,
                                   size_t *width, bool *sign) {
    const MinicCoreType *type;
    unsigned int bits;
    if (width == NULL || sign == NULL) return false;
    id = minic_core_type_unqualified(context, id);
    type = minic_core_type_get(context, id);
    if (type == NULL || type->kind != MINIC_CORE_TYPE_INTEGER)
        return false;
    bits = type->value.integer.bit_width;
    if (bits == 0U || (bits > 32U && bits != 64U)) return false;
    *width = bits == 64U ? 8U : ((size_t)bits + 7U) / 8U;
    *sign = type->value.integer.is_signed;
    return (*width == 1U || *width == 2U || *width == 4U || *width == 8U) &&
           type->has_layout && type->size == *width;
}

static bool core_scalar_float_format(const MinicCoreTypeContext *context,
                                     MinicCoreTypeId id,
                                     MinicCoreFloatFormat format) {
    const MinicCoreType *type;
    id = minic_core_type_unqualified(context, id);
    type = minic_core_type_get(context, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_FLOAT &&
           type->value.floating.format == format;
}

bool minic_lp64_core_valid_parameter_type(const MinicCoreTypeContext *context,
                                           MinicCoreTypeId id) {
    const MinicCoreType *type;
    size_t width;
    bool sign;
    id = minic_core_type_unqualified(context, id);
    type = minic_core_type_get(context, id);
    return type != NULL &&
           (type->kind == MINIC_CORE_TYPE_POINTER ||
            core_scalar_float_format(context, id, MINIC_CORE_FLOAT_BINARY32) ||
            core_scalar_float_format(context, id, MINIC_CORE_FLOAT_BINARY64) ||
            core_scalar_float_format(context, id, MINIC_CORE_FLOAT_EXTENDED) ||
            (type->kind == MINIC_CORE_TYPE_INTEGER &&
             type->value.integer.bit_width == 128U && type->has_layout) ||
            minic_lp64_core_integer_width(context, id, &width, &sign));
}

bool minic_lp64_core_valid_return_type(const MinicCoreTypeContext *context,
                                       MinicCoreTypeId id) {
    size_t bytes, slots, fp_elements, fp_width;
    return minic_lp64_core_kind(context, id, MINIC_CORE_TYPE_VOID) ||
           core_scalar_float_format(context, id, MINIC_CORE_FLOAT_BINARY64) ||
           core_scalar_float_format(context, id, MINIC_CORE_FLOAT_EXTENDED) ||
           minic_lp64_core_valid_parameter_type(context, id) ||
           (minic_lp64_core_kind(context, id, MINIC_CORE_TYPE_RECORD) &&
            (minic_multi_empty_record_core(context, id) ||
             (minic_multi_gp_record_core(context, id, &bytes, &slots) &&
              slots >= 1U && slots <= 2U) ||
             minic_multi_fp_return_record_core(context, id,
                                               &fp_elements, &fp_width)));
}

bool minic_lp64_core_valid_return_with_indirect(
    const MinicCoreTypeContext *context, MinicCoreTypeId id,
    MinicDirectLP64CoreIndirectRecord policy) {
    size_t bytes;
    return minic_lp64_core_valid_return_type(context, id) ||
           (policy != NULL && minic_lp64_core_kind(context, id, MINIC_CORE_TYPE_RECORD) &&
            policy(context, id, &bytes) && bytes > 16U);
}

bool minic_lp64_valid_int(MinicType type) {
    return minic_type_equal(type, minic_type_int());
}

bool minic_lp64_valid_int_or_bool(MinicType type) {
    return minic_lp64_valid_int(type) || minic_type_equal(type, minic_type_bool());
}

bool minic_lp64_scalar_integer_width(const MinicC0Program *program,
                                 const MinicTargetInfo *target, MinicType type,
                                 size_t *width, bool *sign) {
    unsigned int bits;
    MinicType effective;
    if (width == NULL || sign == NULL || !minic_type_is_integer(type) ||
        !minic_c0_type_effective_integer_type(program, type, &effective) ||
        !minic_target_info_integer_width(target, program, type, &bits) ||
        bits == 0U || (bits > 32U && bits != 64U))
        return false;
    /* Preserve the existing bit-width-to-byte rounding for narrow integer
     * semantic types; 33..63-bit integers are still not supported. */
    *width = bits == 64U ? 8U : (bits + 7U) / 8U;
    *sign = minic_type_is_signed_integer(effective);
    return *width == 1U || *width == 2U || *width == 4U || *width == 8U;
}

bool minic_lp64_register64_type(const MinicC0Program *program,
                            const MinicTargetInfo *target, MinicType type) {
    size_t width;
    bool sign;
    return minic_type_is_pointer(type) ||
           (minic_lp64_scalar_integer_width(program, target, type, &width, &sign) &&
            width == 8U);
}

bool minic_lp64_valid_parameter_type(const MinicC0Program *program,
                                 const MinicTargetInfo *target, MinicType type) {
    size_t width;
    bool sign;
    return minic_type_is_pointer(type) || minic_type_is_double(type) ||
           minic_type_is_float(type) || minic_type_is_long_double(type) ||
           minic_type_is_int128_integer(type) ||
           minic_lp64_scalar_integer_width(program, target, type, &width, &sign);
}

bool minic_lp64_valid_return_type(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type) {
    size_t bytes, slots, fp_elements, fp_width;
    /* AAPCS64 and LoongArch64 LP64D return integer-class aggregates up to
     * 16 bytes in a0/a1 (x0/x1). Reuse the SAME conservative integral-only,
     * natural-alignment ABI classification as outgoing aggregate calls.
     * HFA/vector or indirect large records remain excluded. */
    return minic_type_is_void(type) || minic_type_is_double(type) ||
           minic_type_is_long_double(type) ||
           minic_lp64_valid_parameter_type(program, target, type) ||
           (minic_type_is_record(type) &&
            (minic_multi_empty_record(program, target, type) ||
             (minic_multi_gp_record(program, target, type, &bytes, &slots) &&
              slots >= 1U && slots <= 2U) ||
             minic_multi_fp_return_record(program, target, type,
                                           &fp_elements, &fp_width)));
}

bool minic_lp64_valid_return_with_indirect(
    const MinicC0Program *program, const MinicTargetInfo *target,
    MinicType type, MinicDirectLP64IndirectRecord policy) {
    size_t bytes;
    return minic_lp64_valid_return_type(program, target, type) ||
           (policy != NULL && minic_type_is_record(type) &&
            policy(program, target, type, &bytes) && bytes > 16U);
}
