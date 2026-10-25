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

/* RV32 Linux ILP32; semantic integer conversions must not inherit LP64.
 * Plain char is unsigned on RISC-V; wchar_t uses the normal signed int ABI. */
static const MinicTargetIntegerModel minic_rv32_integer_model = {
    .plain_char_sign = MINIC_INTEGER_SIGN_UNSIGNED,
    .semantic_width_bits = {[MINIC_INTEGER_RANK_BOOL] = 1U,
                            [MINIC_INTEGER_RANK_CHAR] = 8U,
                            [MINIC_INTEGER_RANK_SHORT] = 16U,
                            [MINIC_INTEGER_RANK_INT] = 32U,
                            [MINIC_INTEGER_RANK_LONG] = 32U,
                            [MINIC_INTEGER_RANK_LONG_LONG] = 64U,
                            [MINIC_INTEGER_RANK_INT128] = 128U},
};

/* LP64, signed plain char and signed 32-bit wchar_t on x86-64 Linux. */
static const MinicTargetIntegerModel minic_x86_64_linux_integer_model = {
    .plain_char_sign = MINIC_INTEGER_SIGN_SIGNED,
    .semantic_width_bits = {[MINIC_INTEGER_RANK_BOOL] = 1U,
                            [MINIC_INTEGER_RANK_CHAR] = 8U,
                            [MINIC_INTEGER_RANK_SHORT] = 16U,
                            [MINIC_INTEGER_RANK_INT] = 32U,
                            [MINIC_INTEGER_RANK_LONG] = 64U,
                            [MINIC_INTEGER_RANK_LONG_LONG] = 64U,
                            [MINIC_INTEGER_RANK_INT128] = 128U},
};

/* AAPCS64 (Linux LP64): plain char unsigned, 32-bit unsigned wchar_t.
 * Do not conflate this language model with ARM's register-call ABI. */
static const MinicTargetIntegerModel minic_aarch64_linux_integer_model = {
    .plain_char_sign = MINIC_INTEGER_SIGN_UNSIGNED,
    .semantic_width_bits = {[MINIC_INTEGER_RANK_BOOL] = 1U,
                            [MINIC_INTEGER_RANK_CHAR] = 8U,
                            [MINIC_INTEGER_RANK_SHORT] = 16U,
                            [MINIC_INTEGER_RANK_INT] = 32U,
                            [MINIC_INTEGER_RANK_LONG] = 64U,
                            [MINIC_INTEGER_RANK_LONG_LONG] = 64U,
                            [MINIC_INTEGER_RANK_INT128] = 128U},
};

/* LoongArch ELF lp64d has signed plain char (unlike AAPCS64). */
static const MinicTargetIntegerModel minic_loongarch64_linux_integer_model = {
    .plain_char_sign = MINIC_INTEGER_SIGN_SIGNED,
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

const MinicTargetInfo *minic_target_info_for_architecture(
    MinicTargetArchitecture architecture) {
    static MinicTargetInfo rv64_target;
    static MinicTargetInfo rv32_target;
    static MinicTargetInfo x86_64_target;
    static MinicTargetInfo aarch64_target;
    static MinicTargetInfo loongarch64_target;

    if (architecture == MINIC_TARGET_ARCH_RISCV32) {
        if (rv32_target.data_layout == NULL) {
            rv32_target.architecture = MINIC_TARGET_ARCH_RISCV32;
            rv32_target.riscv_isa.xlen = 32U;
            rv32_target.riscv_isa.abi = MINIC_RISCV_ABI_ILP32;
            rv32_target.riscv_isa.strict_isa = true;
            rv32_target.data_layout = minic_riscv32_linux_data_layout();
            rv32_target.integer_model = &minic_rv32_integer_model;
            rv32_target.wide_character_type = minic_type_int();
            rv32_target.libc_size_type = minic_type_unsigned_int();
            rv32_target.word_integer_rank = MINIC_INTEGER_RANK_INT;
            rv32_target.gnu_sizeof_void_is_one = true;
            rv32_target.gnu_sizeof_function_is_one = true;
            /* RV32 calls need their own ILP32 register-pair/frame lowering.
             * Never copy an LP64 cursor until that machine side is qualified. */
            rv32_target.variadic_scalar_read_kind =
                MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT;
            rv32_target.variadic_linear_slot_bytes = 4U;
            rv32_target.variadic_copy_kind =
                MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT;
            /* The ILP32 machine emitter always saves ra in its own frame.
             * Only level zero is qualified; never expose caller unwinding. */
            rv32_target.call_frame_return_address_level0 = true;
            rv32_target.call_frame_frame_address_level0 = true;
        }
        return &rv32_target;
    }
    if (architecture == MINIC_TARGET_ARCH_RISCV64) {
        if (rv64_target.data_layout == NULL) {
            rv64_target.architecture = MINIC_TARGET_ARCH_RISCV64;
            rv64_target.riscv_isa.xlen = 64U;
            rv64_target.riscv_isa.abi = MINIC_RISCV_ABI_LP64D;
            rv64_target.riscv_isa.strict_isa = false;
            rv64_target.data_layout = minic_default_data_layout();
            rv64_target.integer_model = &minic_rv64_integer_model;
            rv64_target.wide_character_type = minic_type_unsigned_short();
            rv64_target.libc_size_type = minic_type_unsigned_long();
            rv64_target.word_integer_rank = MINIC_INTEGER_RANK_LONG;
            rv64_target.language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS;
            rv64_target.gnu_sizeof_void_is_one = true;
            rv64_target.gnu_sizeof_function_is_one = true;
            rv64_target.variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT;
            rv64_target.variadic_linear_slot_bytes = 8U;
            rv64_target.variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT;
            rv64_target.call_frame_return_address_level0 = true;
            rv64_target.call_frame_frame_address_level0 = true;
        }
        return &rv64_target;
    }
    if (architecture == MINIC_TARGET_ARCH_X86_64) {
        if (x86_64_target.data_layout == NULL) {
            x86_64_target.architecture = MINIC_TARGET_ARCH_X86_64;
            x86_64_target.data_layout = minic_x86_64_linux_data_layout();
            x86_64_target.integer_model = &minic_x86_64_linux_integer_model;
            x86_64_target.wide_character_type = minic_type_int();
            x86_64_target.libc_size_type = minic_type_unsigned_long();
            x86_64_target.word_integer_rank = MINIC_INTEGER_RANK_LONG;
            x86_64_target.language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS | MINIC_TARGET_LANG_ABI_STATE_VA_LIST_CURSOR;
            x86_64_target.gnu_sizeof_void_is_one = true;
            x86_64_target.gnu_sizeof_function_is_one = true;
            x86_64_target.variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR;
            x86_64_target.variadic_linear_slot_bytes = 0U;
            x86_64_target.variadic_copy_kind = MINIC_VARIADIC_COPY_ABI_STATE;
            /* Machine-level frame address and return address support awaits
             * the x86-64 backend.  Do not accept these builtins prematurely. */
        }
        return &x86_64_target;
    }
    if (architecture == MINIC_TARGET_ARCH_AARCH64) {
        if (aarch64_target.data_layout == NULL) {
            aarch64_target.architecture = MINIC_TARGET_ARCH_AARCH64;
            aarch64_target.data_layout = minic_aarch64_linux_data_layout();
            aarch64_target.integer_model = &minic_aarch64_linux_integer_model;
            aarch64_target.wide_character_type = minic_type_unsigned_int();
            aarch64_target.libc_size_type = minic_type_unsigned_long();
            aarch64_target.word_integer_rank = MINIC_INTEGER_RANK_LONG;
            aarch64_target.language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS | MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS;
            aarch64_target.gnu_sizeof_void_is_one = true;
            aarch64_target.gnu_sizeof_function_is_one = true;
            aarch64_target.variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR;
            aarch64_target.variadic_linear_slot_bytes = 0U;
            aarch64_target.variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT;
        }
        return &aarch64_target;
    }
    if (architecture == MINIC_TARGET_ARCH_LOONGARCH64) {
        if (loongarch64_target.data_layout == NULL) {
            loongarch64_target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
            loongarch64_target.data_layout = minic_loongarch64_linux_data_layout();
            loongarch64_target.integer_model = &minic_loongarch64_linux_integer_model;
            loongarch64_target.wide_character_type = minic_type_int();
            loongarch64_target.libc_size_type = minic_type_unsigned_long();
            loongarch64_target.word_integer_rank = MINIC_INTEGER_RANK_LONG;
            loongarch64_target.language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS;
            loongarch64_target.gnu_sizeof_void_is_one = true;
            loongarch64_target.gnu_sizeof_function_is_one = true;
            loongarch64_target.variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT;
            loongarch64_target.variadic_linear_slot_bytes = 8U;
            loongarch64_target.variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT;
        }
        return &loongarch64_target;
    }
    return NULL;
}

const MinicTargetInfo *minic_default_target_info(void) {
    return minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64);
}

const MinicTargetInfo *minic_target_info_from_name(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    if (strcmp(name, "riscv32-linux") == 0) {
        return minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV32);
    }
    if (strcmp(name, "riscv64-linux") == 0) {
        return minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64);
    }
    if (strcmp(name, "x86_64-linux") == 0) {
        return minic_target_info_for_architecture(MINIC_TARGET_ARCH_X86_64);
    }
    if (strcmp(name, "aarch64-linux") == 0) {
        return minic_target_info_for_architecture(MINIC_TARGET_ARCH_AARCH64);
    }
    if (strcmp(name, "loongarch64-linux") == 0) {
        return minic_target_info_for_architecture(MINIC_TARGET_ARCH_LOONGARCH64);
    }
    return NULL;
}

const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target) {
    return target == NULL ? NULL : target->data_layout;
}

const MinicTargetIntegerModel *minic_target_info_integer_model(const MinicTargetInfo *target) {
    return target == NULL ? NULL : target->integer_model;
}


/* The language admission gate is intentionally independent of the emitter.
 * A declaration may be parsed while its arithmetic and ABI remain unsupported. */
bool minic_target_info_language_type_supported(const MinicTargetInfo *target,
                                               MinicTypeBaseKind kind) {
    unsigned int required;
    if (target == NULL) return false;
    switch (kind) {
    case MINIC_TYPE_BASE_FLOAT128:
    case MINIC_TYPE_BASE_FLOAT32:
    case MINIC_TYPE_BASE_FLOAT64:
    case MINIC_TYPE_BASE_FLOAT32X:
    case MINIC_TYPE_BASE_FLOAT64X:
        required = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS;
        break;
    case MINIC_TYPE_BASE_AARCH64_FLOAT32X4:
    case MINIC_TYPE_BASE_AARCH64_FLOAT64X2:
    case MINIC_TYPE_BASE_AARCH64_SVE_FLOAT32:
    case MINIC_TYPE_BASE_AARCH64_SVE_FLOAT64:
    case MINIC_TYPE_BASE_AARCH64_SVE_BOOL:
        required = MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS;
        break;
    case MINIC_TYPE_BASE_X86_VA_LIST_CURSOR:
        required = MINIC_TARGET_LANG_ABI_STATE_VA_LIST_CURSOR;
        break;
    default:
        return false;
    }
    return (target->language_type_capabilities & required) != 0U;
}

/* Opaque header-only identities are registered at the target boundary. No
 * frontend CPU-name test or implicit vector layout follows from recognition. */
bool minic_target_info_named_builtin_type(const MinicTargetInfo *target,
                                          const char *spelling, size_t length,
                                          MinicType *type) {
    static const struct {
        const char *spelling;
        MinicTypeBaseKind kind;
    } names[] = {
        {"__Float32x4_t", MINIC_TYPE_BASE_AARCH64_FLOAT32X4},
        {"__Float64x2_t", MINIC_TYPE_BASE_AARCH64_FLOAT64X2},
        {"__SVFloat32_t", MINIC_TYPE_BASE_AARCH64_SVE_FLOAT32},
        {"__SVFloat64_t", MINIC_TYPE_BASE_AARCH64_SVE_FLOAT64},
        {"__SVBool_t", MINIC_TYPE_BASE_AARCH64_SVE_BOOL}
    };
    size_t index;
    if (target == NULL || spelling == NULL) return false;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (length == strlen(names[index].spelling) &&
            memcmp(spelling, names[index].spelling, length) == 0 &&
            minic_target_info_language_type_supported(target, names[index].kind)) {
            if (type != NULL) {
                *type = minic_type_aarch64_header_builtin(names[index].kind);
            }
            return true;
        }
    }
    return false;
}

/* Only the x86 SysV cursor has a distinct internal identity today. Other
 * existing targets preserve their canonical sequential void-pointer model. */
bool minic_target_info_builtin_va_list_type(const MinicTargetInfo *target,
                                            MinicType *type) {
    if (type == NULL) return false;
    if (minic_target_info_language_type_supported(
            target, MINIC_TYPE_BASE_X86_VA_LIST_CURSOR)) {
        *type = minic_type_x86_va_list_cursor();
        return true;
    }
    return minic_type_pointer_to(minic_type_void(), type);
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

bool minic_target_info_word_integer_type(const MinicTargetInfo *target,
                                         MinicIntegerSign sign,
                                         MinicType *type) {
    if (target == NULL || target->integer_model == NULL || type == NULL ||
        target->word_integer_rank <= MINIC_INTEGER_RANK_NONE ||
        target->word_integer_rank > MINIC_INTEGER_RANK_INT128 ||
        (sign != MINIC_INTEGER_SIGN_SIGNED && sign != MINIC_INTEGER_SIGN_UNSIGNED)) {
        return false;
    }
    return integer_type_with_rank(sign, target->word_integer_rank, type);
}

bool minic_target_info_sizeof_type(const MinicTargetInfo *target,
                                   const MinicC0Program *program,
                                   MinicType type,
                                   size_t *size) {
    size_t alignment;

    if (target == NULL || program == NULL || size == NULL) {
        return false;
    }
    /* The x86 runtime cursor proxy occupies one machine pointer but
     * __builtin_va_list is really struct __va_list_tag[1] (24 bytes).
     * Until that array object/parameter ABI model exists, never return 8
     * for sizeof(va_list) or sizeof(local_va_list). This catches typedefs
     * and expressions, unlike spelling-based parser diagnostics. */
    if (minic_type_is_x86_va_list_cursor(type) &&
        target->architecture == MINIC_TARGET_ARCH_X86_64) {
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
    MinicType effective_type;

    if (target == NULL || program == NULL ||
        !minic_c0_type_effective_integer_type(program, type, &effective_type)) {
        return false;
    }
    return integer_model_semantic_width(target->integer_model, effective_type, bits);
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

bool minic_target_info_integer_promotion_for_program(const MinicTargetInfo *target,
                                                     const MinicC0Program *program,
                                                     MinicType type,
                                                     MinicType *result) {
    MinicType effective_type;

    return target != NULL && program != NULL && result != NULL &&
           minic_c0_type_effective_integer_type(program, type, &effective_type) &&
           minic_target_info_integer_promotion(target, effective_type, result);
}

bool minic_target_info_integer_common_for_program(const MinicTargetInfo *target,
                                                  const MinicC0Program *program,
                                                  MinicType left,
                                                  MinicType right,
                                                  MinicType *result) {
    MinicType effective_left;
    MinicType effective_right;

    return target != NULL && program != NULL && result != NULL &&
           minic_c0_type_effective_integer_type(program, left, &effective_left) &&
           minic_c0_type_effective_integer_type(program, right, &effective_right) &&
           minic_target_info_integer_common(target, effective_left, effective_right, result);
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
        (base != MINIC_INTEGER_LITERAL_BASE_BINARY &&
         base != MINIC_INTEGER_LITERAL_BASE_DECIMAL &&
         base != MINIC_INTEGER_LITERAL_BASE_OCTAL &&
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
    if (target == NULL || name == NULL ||
        (target->architecture != MINIC_TARGET_ARCH_RISCV64 &&
         target->architecture != MINIC_TARGET_ARCH_RISCV32)) {
        return false;
    }
    /* Both RISC-V backends reserve sp/tp architecturally. RV32 shares
     * the SAME Core fixed-register value semantics as RV64, but has its own
     * ILP32 machine load/store and frame contract; admit no scratch or ABI
     * argument register binding without a dedicated target owner.
     * The current RV64 backend uses most caller/scratch registers internally.
     * Only the architectural stack/thread registers observed in unchanged Linux
     * are safe fixed bindings until register allocation becomes target-aware. */
    return (name_length == 2U && memcmp(name, "tp", 2U) == 0) ||
           (name_length == 2U && memcmp(name, "sp", 2U) == 0);
}

bool minic_target_info_local_fixed_register_supported(const MinicTargetInfo *target,
                                                      const char *name,
                                                      size_t name_length) {
    if (target == NULL ||
        (target->architecture != MINIC_TARGET_ARCH_RISCV64 &&
         target->architecture != MINIC_TARGET_ARCH_RISCV32) || name == NULL) {
        return false;
    }
    return name_length == 2U && name[0] == 'a' && name[1] >= '0' && name[1] <= '7';
}

bool minic_target_info_inline_asm_register_clobber_supported(const MinicTargetInfo *target,
                                                             const char *name,
                                                             size_t name_length) {
    if (target == NULL || name == NULL) return false;
    /* x86 only: the core and machine backends independently check that
     * flags and RDX are genuinely clobbered, and preserve SysV RBX. */
    if (target->architecture == MINIC_TARGET_ARCH_X86_64)
        return name_length == 2U &&
               (memcmp(name, "cc", 2U) == 0 || memcmp(name, "dx", 2U) == 0);
    if (target->architecture == MINIC_TARGET_ARCH_RISCV32) {
        /* Admission is separate from allocation: an explicit t-register
         * clobber must be respected by the RV32 source-asm register planner.
         * RV32 caller-clobber sets match the base RISC-V integer ABI. */
        return name_length == 2U &&
               ((name[0] == 'a' && name[1] >= '0' && name[1] <= '7') ||
                (name[0] == 't' && name[1] >= '0' && name[1] <= '6'));
    }
    if (target->architecture != MINIC_TARGET_ARCH_RISCV64) return false;
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
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_RISCV64 || constraint == NULL) {
        return false;
    }
    /* RISC-V GCC constraint I is a signed 12-bit integer immediate. */
    return constraint_length == 1U && constraint[0] == 'I' && value >= -2048 && value <= 2047;
}

/* The ABI owns the state-cursor representation; Core never tests an ISA.
 * Other structured cursor ABIs must explicitly implement their own policy. */
bool minic_target_info_variadic_state_copy_type_supported(
    const MinicTargetInfo *target, MinicType type) {
    if (!target || target->variadic_copy_kind != MINIC_VARIADIC_COPY_ABI_STATE)
        return false;
    switch (target->architecture) {
    case MINIC_TARGET_ARCH_X86_64:
        return minic_type_is_x86_va_list_cursor(type);
    default:
        return false;
    }
}
