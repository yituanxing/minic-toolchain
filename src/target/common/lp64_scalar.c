#include "target/common/lp64_scalar.h"
#include "target/common/lp64_aggregate.h"

/* Shared exact LP64 scalar gates from accepted target emitters. */

bool minic_lp64_valid_int(MinicType type) {
    return minic_type_equal(type, minic_type_int());
}

bool minic_lp64_valid_int_or_bool(MinicType type) {
    return minic_lp64_valid_int(type) || minic_type_equal(type, minic_type_bool());
}

static bool minic_lp64_small_integer_width(const MinicC0Program *program,
                                const MinicTargetInfo *target, MinicType type,
                                size_t *width, bool *sign) {
    unsigned int bits;
    MinicType effective;
    if (width == NULL || sign == NULL ||
        !minic_type_is_integer(type) ||
        !minic_c0_type_effective_integer_type(program, type, &effective) ||
        !minic_target_info_integer_width(target, program, type, &bits) ||
        bits == 0U || bits > 32U) return false;
    *width = (bits + 7U) / 8U;
    *sign = minic_type_is_signed_integer(effective);
    return *width == 1U || *width == 2U || *width == 4U;
}

bool minic_lp64_scalar_integer_width(const MinicC0Program *program,
                                 const MinicTargetInfo *target, MinicType type,
                                 size_t *width, bool *sign) {
    unsigned int bits;
    MinicType effective;
    if (minic_lp64_small_integer_width(program, target, type, width, sign)) return true;
    if (width == NULL || sign == NULL || !minic_type_is_integer(type) ||
        !minic_c0_type_effective_integer_type(program, type, &effective) ||
        !minic_target_info_integer_width(target, program, type, &bits) ||
        bits != 64U) return false;
    *width = 8U;
    *sign = minic_type_is_signed_integer(effective);
    return true;
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
            ((minic_multi_gp_record(program, target, type, &bytes, &slots) &&
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
