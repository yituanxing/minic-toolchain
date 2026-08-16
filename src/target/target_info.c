#include "target/target_info.h"

#include <stdint.h>
#include <string.h>

static const MinicTargetIntegerModel minic_rv64_integer_model = {
    .plain_char_sign = MINIC_INTEGER_SIGN_UNSIGNED,
    .semantic_width_bits = {[MINIC_INTEGER_RANK_BOOL] = 1U,
                            [MINIC_INTEGER_RANK_CHAR] = 8U,
                            [MINIC_INTEGER_RANK_SHORT] = 16U,
                            [MINIC_INTEGER_RANK_INT] = 32U,
                            [MINIC_INTEGER_RANK_LONG] = 64U,
                            [MINIC_INTEGER_RANK_LONG_LONG] = 64U,
                            [MINIC_INTEGER_RANK_INT128] = 128U},
};

const char *const minic_riscv64_argument_registers[8] = {
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};

static bool
integer_type_with_rank(MinicIntegerSign sign, MinicIntegerRank rank, MinicType *result) {
    if (result == NULL) {
        return false;
    }
    switch (rank) {
    case MINIC_INTEGER_RANK_BOOL:
        if (sign != MINIC_INTEGER_SIGN_UNSIGNED) {
            return false;
        }
        *result = minic_type_bool();
        return true;
    case MINIC_INTEGER_RANK_CHAR:
        *result = sign == MINIC_INTEGER_SIGN_SIGNED     ? minic_type_signed_char()
                  : sign == MINIC_INTEGER_SIGN_UNSIGNED ? minic_type_unsigned_char()
                                                        : minic_type_void();
        return sign != MINIC_INTEGER_SIGN_NONE;
    case MINIC_INTEGER_RANK_SHORT:
        *result = sign == MINIC_INTEGER_SIGN_SIGNED     ? minic_type_short()
                  : sign == MINIC_INTEGER_SIGN_UNSIGNED ? minic_type_unsigned_short()
                                                        : minic_type_void();
        return sign != MINIC_INTEGER_SIGN_NONE;
    case MINIC_INTEGER_RANK_INT:
        *result = sign == MINIC_INTEGER_SIGN_SIGNED     ? minic_type_int()
                  : sign == MINIC_INTEGER_SIGN_UNSIGNED ? minic_type_unsigned_int()
                                                        : minic_type_void();
        return sign != MINIC_INTEGER_SIGN_NONE;
    case MINIC_INTEGER_RANK_LONG:
        *result = sign == MINIC_INTEGER_SIGN_SIGNED     ? minic_type_long()
                  : sign == MINIC_INTEGER_SIGN_UNSIGNED ? minic_type_unsigned_long()
                                                        : minic_type_void();
        return sign != MINIC_INTEGER_SIGN_NONE;
    case MINIC_INTEGER_RANK_LONG_LONG:
        *result = sign == MINIC_INTEGER_SIGN_SIGNED     ? minic_type_long_long()
                  : sign == MINIC_INTEGER_SIGN_UNSIGNED ? minic_type_unsigned_long_long()
                                                        : minic_type_void();
        return sign != MINIC_INTEGER_SIGN_NONE;
    case MINIC_INTEGER_RANK_INT128:
        *result = sign == MINIC_INTEGER_SIGN_SIGNED     ? minic_type_int128()
                  : sign == MINIC_INTEGER_SIGN_UNSIGNED ? minic_type_unsigned_int128()
                                                        : minic_type_void();
        return sign != MINIC_INTEGER_SIGN_NONE;
    case MINIC_INTEGER_RANK_NONE:
        return false;
    }
    return false;
}

static bool integer_model_semantic_width(const MinicTargetIntegerModel *model,
                                         MinicType type,
                                         unsigned int *bits) {
    size_t rank;

    if (model == NULL || bits == NULL || !minic_type_is_integer(type) ||
        type.integer_rank <= MINIC_INTEGER_RANK_NONE ||
        type.integer_rank > MINIC_INTEGER_RANK_INT128) {
        return false;
    }
    rank = (size_t)type.integer_rank;
    if (model->semantic_width_bits[rank] == 0U) {
        return false;
    }
    *bits = model->semantic_width_bits[rank];
    return true;
}

static bool integer_model_effective_sign(const MinicTargetIntegerModel *model,
                                         MinicType type,
                                         MinicIntegerSign *sign) {
    if (model == NULL || sign == NULL || !minic_type_is_integer(type)) {
        return false;
    }
    *sign = minic_type_is_plain_char(type) ? model->plain_char_sign : type.integer_sign;
    return *sign == MINIC_INTEGER_SIGN_SIGNED || *sign == MINIC_INTEGER_SIGN_UNSIGNED;
}

static bool signed_type_represents_unsigned(const MinicTargetIntegerModel *model,
                                            MinicType signed_type,
                                            MinicType unsigned_type) {
    unsigned int signed_bits;
    unsigned int unsigned_bits;
    MinicIntegerSign signed_sign;
    MinicIntegerSign unsigned_sign;

    return integer_model_effective_sign(model, signed_type, &signed_sign) &&
           integer_model_effective_sign(model, unsigned_type, &unsigned_sign) &&
           signed_sign == MINIC_INTEGER_SIGN_SIGNED &&
           unsigned_sign == MINIC_INTEGER_SIGN_UNSIGNED &&
           integer_model_semantic_width(model, signed_type, &signed_bits) &&
           integer_model_semantic_width(model, unsigned_type, &unsigned_bits) &&
           signed_bits > unsigned_bits;
}

static bool
integer_literal_fits(const MinicTargetIntegerModel *model, MinicType type, uint64_t value) {
    unsigned int bits;
    MinicIntegerSign sign;

    if (!integer_model_semantic_width(model, type, &bits) ||
        !integer_model_effective_sign(model, type, &sign) || bits == 0U) {
        return false;
    }
    if (sign == MINIC_INTEGER_SIGN_UNSIGNED) {
        if (bits >= 64U) {
            return true;
        }
        return value <= (UINT64_C(1) << bits) - UINT64_C(1);
    }
    if (bits > 64U) {
        return true;
    }
    if (bits == 64U) {
        return value <= (uint64_t)INT64_MAX;
    }
    return value <= (UINT64_C(1) << (bits - 1U)) - UINT64_C(1);
}

const MinicTargetInfo *minic_default_target_info(void) {
    static MinicTargetInfo target;

    if (target.data_layout == NULL) {
        target.data_layout = minic_default_data_layout();
        target.integer_model = &minic_rv64_integer_model;
        target.wide_character_type = minic_type_unsigned_short();
        target.gnu_sizeof_void_is_one = true;
        target.gnu_sizeof_function_is_one = true;
        target.call_frame_return_address_level0 = true;
        target.call_frame_frame_address_level0 = true;
    }
    return &target;
}

const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target) {
    return target == NULL ? NULL : target->data_layout;
}

const MinicTargetIntegerModel *minic_target_info_integer_model(const MinicTargetInfo *target) {
    return target == NULL ? NULL : target->integer_model;
}

bool minic_target_info_plain_char_sign(const MinicTargetInfo *target, MinicIntegerSign *sign) {
    if (target == NULL || target->integer_model == NULL || sign == NULL ||
        (target->integer_model->plain_char_sign != MINIC_INTEGER_SIGN_SIGNED &&
         target->integer_model->plain_char_sign != MINIC_INTEGER_SIGN_UNSIGNED)) {
        return false;
    }
    *sign = target->integer_model->plain_char_sign;
    return true;
}

bool minic_target_info_plain_char_type(const MinicTargetInfo *target, MinicType *type) {
    MinicIntegerSign sign;

    if (type == NULL || !minic_target_info_plain_char_sign(target, &sign)) {
        return false;
    }
    *type = minic_type_char();
    type->integer_sign = sign;
    return true;
}

bool minic_target_info_wide_character_type(const MinicTargetInfo *target, MinicType *type) {
    if (target == NULL || type == NULL || !minic_type_is_integer(target->wide_character_type)) {
        return false;
    }
    *type = target->wide_character_type;
    return true;
}

bool minic_target_info_sizeof_type(const MinicTargetInfo *target,
                                   const MinicC0Program *program,
                                   MinicType type,
                                   size_t *size) {
    size_t alignment;

    if (target == NULL || program == NULL || size == NULL) {
        return false;
    }
    if (minic_type_is_void(type)) {
        if (!target->gnu_sizeof_void_is_one) {
            return false;
        }
        *size = 1U;
        return true;
    }
    if (minic_type_is_function(type)) {
        if (!target->gnu_sizeof_function_is_one) {
            return false;
        }
        *size = 1U;
        return true;
    }
    return minic_data_layout_type(target->data_layout, program, type, size, &alignment);
}

bool minic_target_info_integer_width(const MinicTargetInfo *target,
                                     const MinicC0Program *program,
                                     MinicType type,
                                     unsigned int *bits) {
    if (target == NULL || program == NULL) {
        return false;
    }
    return integer_model_semantic_width(target->integer_model, type, bits);
}

bool minic_target_info_integer_promotion(const MinicTargetInfo *target,
                                         MinicType type,
                                         MinicType *result) {
    const MinicTargetIntegerModel *model;
    MinicIntegerSign sign;
    unsigned int int_bits;
    unsigned int source_bits;

    if (target == NULL || result == NULL || !minic_type_is_integer(type)) {
        return false;
    }
    model = target->integer_model;
    if (!integer_model_effective_sign(model, type, &sign) ||
        !integer_model_semantic_width(model, type, &source_bits) ||
        !integer_model_semantic_width(model, minic_type_int(), &int_bits)) {
        return false;
    }
    if (type.integer_rank < MINIC_INTEGER_RANK_INT) {
        if (type.integer_rank == MINIC_INTEGER_RANK_BOOL ||
            (sign == MINIC_INTEGER_SIGN_SIGNED && source_bits <= int_bits) ||
            (sign == MINIC_INTEGER_SIGN_UNSIGNED && source_bits < int_bits)) {
            *result = minic_type_int();
        } else {
            *result = minic_type_unsigned_int();
        }
        return true;
    }
    return integer_type_with_rank(sign, type.integer_rank, result);
}

bool minic_target_info_integer_common(const MinicTargetInfo *target,
                                      MinicType left,
                                      MinicType right,
                                      MinicType *result) {
    MinicType promoted_left;
    MinicType promoted_right;
    MinicType signed_type;
    MinicType unsigned_type;
    MinicIntegerSign left_sign;
    MinicIntegerSign right_sign;

    if (target == NULL || result == NULL ||
        !minic_target_info_integer_promotion(target, left, &promoted_left) ||
        !minic_target_info_integer_promotion(target, right, &promoted_right) ||
        !integer_model_effective_sign(target->integer_model, promoted_left, &left_sign) ||
        !integer_model_effective_sign(target->integer_model, promoted_right, &right_sign)) {
        return false;
    }
    if (minic_type_equal(promoted_left, promoted_right)) {
        *result = promoted_left;
        return true;
    }
    if (left_sign == right_sign) {
        return integer_type_with_rank(left_sign,
                                      promoted_left.integer_rank > promoted_right.integer_rank
                                          ? promoted_left.integer_rank
                                          : promoted_right.integer_rank,
                                      result);
    }

    signed_type = left_sign == MINIC_INTEGER_SIGN_SIGNED ? promoted_left : promoted_right;
    unsigned_type = left_sign == MINIC_INTEGER_SIGN_UNSIGNED ? promoted_left : promoted_right;
    if (unsigned_type.integer_rank >= signed_type.integer_rank) {
        return integer_type_with_rank(
            MINIC_INTEGER_SIGN_UNSIGNED, unsigned_type.integer_rank, result);
    }
    if (signed_type_represents_unsigned(target->integer_model, signed_type, unsigned_type)) {
        *result = signed_type;
        return true;
    }
    return integer_type_with_rank(MINIC_INTEGER_SIGN_UNSIGNED, signed_type.integer_rank, result);
}

bool minic_target_info_integer_literal_type(const MinicTargetInfo *target,
                                            MinicIntegerLiteralBase base,
                                            bool has_unsigned_suffix,
                                            unsigned int long_count,
                                            uint64_t value,
                                            MinicType *result) {
    MinicType candidates[6];
    size_t candidate_count;
    size_t index;
    bool decimal;

    if (target == NULL || target->integer_model == NULL || result == NULL || long_count > 2U ||
        (base != MINIC_INTEGER_LITERAL_BASE_DECIMAL && base != MINIC_INTEGER_LITERAL_BASE_OCTAL &&
         base != MINIC_INTEGER_LITERAL_BASE_HEXADECIMAL)) {
        return false;
    }
    candidate_count = 0U;
    decimal = base == MINIC_INTEGER_LITERAL_BASE_DECIMAL;

    if (has_unsigned_suffix) {
        if (long_count == 0U) {
            candidates[candidate_count++] = minic_type_unsigned_int();
        }
        if (long_count <= 1U) {
            candidates[candidate_count++] = minic_type_unsigned_long();
        }
        candidates[candidate_count++] = minic_type_unsigned_long_long();
    } else if (long_count == 0U) {
        candidates[candidate_count++] = minic_type_int();
        if (!decimal) {
            candidates[candidate_count++] = minic_type_unsigned_int();
        }
        candidates[candidate_count++] = minic_type_long();
        if (!decimal) {
            candidates[candidate_count++] = minic_type_unsigned_long();
        }
        candidates[candidate_count++] = minic_type_long_long();
        if (!decimal) {
            candidates[candidate_count++] = minic_type_unsigned_long_long();
        }
    } else if (long_count == 1U) {
        candidates[candidate_count++] = minic_type_long();
        if (!decimal) {
            candidates[candidate_count++] = minic_type_unsigned_long();
        }
        candidates[candidate_count++] = minic_type_long_long();
        if (!decimal) {
            candidates[candidate_count++] = minic_type_unsigned_long_long();
        }
    } else {
        candidates[candidate_count++] = minic_type_long_long();
        if (!decimal) {
            candidates[candidate_count++] = minic_type_unsigned_long_long();
        }
    }

    for (index = 0U; index < candidate_count; ++index) {
        if (integer_literal_fits(target->integer_model, candidates[index], value)) {
            *result = candidates[index];
            return true;
        }
    }
    return false;
}

bool minic_target_info_call_frame_address_supported(const MinicTargetInfo *target,
                                                    MinicCallFrameAddressKind kind,
                                                    unsigned int level) {
    if (target == NULL || level != 0U) {
        return false;
    }
    switch (kind) {
    case MINIC_CALL_FRAME_ADDRESS_RETURN:
        return target->call_frame_return_address_level0;
    case MINIC_CALL_FRAME_ADDRESS_FRAME:
        return target->call_frame_frame_address_level0;
    }
    return false;
}

bool minic_target_info_fixed_register_supported(const MinicTargetInfo *target,
                                                const char *name,
                                                size_t name_length) {
    if (target == NULL || name == NULL) {
        return false;
    }
    /* The current RV64 backend uses most caller/scratch registers internally.
     * Only the architectural stack/thread registers observed in unchanged Linux
     * are safe fixed bindings until register allocation becomes target-aware. */
    return (name_length == 2U && memcmp(name, "tp", 2U) == 0) ||
           (name_length == 2U && memcmp(name, "sp", 2U) == 0);
}

bool minic_target_info_inline_asm_register_clobber_supported(const MinicTargetInfo *target,
                                                             const char *name,
                                                             size_t name_length) {
    if (target == NULL || name == NULL) {
        return false;
    }
    /* TargetConstraint v1 keeps temporary registers available to the current
     * inline-asm operand allocator while also recognizing the RV64 argument
     * registers as clobber-only physical registers. The backend never allocates
     * a0..a7 to GNU asm operands, so declaring them clobbered cannot alias an
     * allocator-owned operand. Broader saved/special register classes remain
     * fail-closed until their preservation contract is explicit. */
    return name_length == 2U && ((name[0] == 't' && name[1] >= '0' && name[1] <= '6') ||
                                 (name[0] == 'a' && name[1] >= '0' && name[1] <= '7'));
}

bool minic_target_info_inline_asm_immediate_constraint_supported(const MinicTargetInfo *target,
                                                                 const char *constraint,
                                                                 size_t constraint_length,
                                                                 int64_t value) {
    if (target == NULL || constraint == NULL) {
        return false;
    }
    /* RISC-V GCC constraint I is a signed 12-bit integer immediate. */
    return constraint_length == 1U && constraint[0] == 'I' && value >= -2048 && value <= 2047;
}
