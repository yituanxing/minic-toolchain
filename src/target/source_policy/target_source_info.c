#include "target/source_policy/target_source_info.h"
#include "target/source_policy/source_policy.h"

#include <stdint.h>

/* Integer widths and signedness are C language-model facts, not calling
 * conventions. These three models may be shared by targets with identical
 * C integer semantics without conflating their ABIs or machine lowering. */
static const MinicTargetIntegerModel minic_lp64_unsigned_char_integer_model = {
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
static const MinicTargetIntegerModel minic_ilp32_unsigned_char_integer_model = {
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
static const MinicTargetIntegerModel minic_lp64_signed_char_integer_model = {
    .plain_char_sign = MINIC_INTEGER_SIGN_SIGNED,
    .semantic_width_bits = {[MINIC_INTEGER_RANK_BOOL] = 1U,
                            [MINIC_INTEGER_RANK_CHAR] = 8U,
                            [MINIC_INTEGER_RANK_SHORT] = 16U,
                            [MINIC_INTEGER_RANK_INT] = 32U,
                            [MINIC_INTEGER_RANK_LONG] = 64U,
                            [MINIC_INTEGER_RANK_LONG_LONG] = 64U,
                            [MINIC_INTEGER_RANK_INT128] = 128U},
};

/* A source descriptor contains only language meaning; ABI frame and ISA
 * capabilities remain exclusive to machine TargetInfo. */
typedef struct MinicTargetSourceDescriptor {
    MinicTargetSourceInfo source;
    MinicIntegerSign wide_character_sign;
    MinicIntegerRank wide_character_rank;
    MinicIntegerRank libc_size_rank;
} MinicTargetSourceDescriptor;

static const MinicTargetSourceDescriptor minic_source_riscv32 = {
    .source = {
        .asm_source_policy = &minic_riscv32_asm_source_policy,
        .integer_model = &minic_ilp32_unsigned_char_integer_model,
        .word_integer_rank = MINIC_INTEGER_RANK_INT,
        .long_double_significand_bits = 113U,
        .gnu_sizeof_void_is_one = true,
        .gnu_sizeof_function_is_one = true,
    },
    .wide_character_sign = MINIC_INTEGER_SIGN_SIGNED,
    .wide_character_rank = MINIC_INTEGER_RANK_INT,
    .libc_size_rank = MINIC_INTEGER_RANK_INT,
};
static const MinicTargetSourceDescriptor minic_source_riscv64 = {
    .source = {
        .asm_source_policy = &minic_riscv64_asm_source_policy,
        .integer_model = &minic_lp64_unsigned_char_integer_model,
        .word_integer_rank = MINIC_INTEGER_RANK_LONG,
        .long_double_significand_bits = 113U,
        .language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS,
        .gnu_sizeof_void_is_one = true,
        .gnu_sizeof_function_is_one = true,
    },
    .wide_character_sign = MINIC_INTEGER_SIGN_SIGNED,
    .wide_character_rank = MINIC_INTEGER_RANK_INT,
    .libc_size_rank = MINIC_INTEGER_RANK_LONG,
};
static const MinicTargetSourceDescriptor minic_source_x86_64 = {
    .source = {
        .asm_source_policy = &minic_x86_64_asm_source_policy,
        .source_language_policy = &minic_x86_64_source_language_policy,
        .va_cursor_source_abi_policy = &minic_x86_64_va_cursor_source_abi_policy,
        .integer_model = &minic_lp64_signed_char_integer_model,
        .word_integer_rank = MINIC_INTEGER_RANK_LONG,
        .long_double_significand_bits = 64U,
        .language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS |
            MINIC_TARGET_LANG_MS_X64_FUNCTION_POINTER_DECLARATIONS |
            MINIC_TARGET_LANG_GNU_FIXED_VECTOR_STORAGE,
        .gnu_sizeof_void_is_one = true,
        .gnu_sizeof_function_is_one = true,
    },
    .wide_character_sign = MINIC_INTEGER_SIGN_SIGNED,
    .wide_character_rank = MINIC_INTEGER_RANK_INT,
    .libc_size_rank = MINIC_INTEGER_RANK_LONG,
};
static const MinicTargetSourceDescriptor minic_source_aarch64 = {
    .source = {
        .asm_source_policy = &minic_aarch64_asm_source_policy,
        .source_language_policy = &minic_aarch64_source_language_policy,
        .va_cursor_source_abi_policy = &minic_aarch64_va_cursor_source_abi_policy,
        .integer_model = &minic_lp64_unsigned_char_integer_model,
        .word_integer_rank = MINIC_INTEGER_RANK_LONG,
        .long_double_significand_bits = 113U,
        .language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS |
            MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS |
            MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
        .gnu_sizeof_void_is_one = true,
        .gnu_sizeof_function_is_one = true,
    },
    .wide_character_sign = MINIC_INTEGER_SIGN_UNSIGNED,
    .wide_character_rank = MINIC_INTEGER_RANK_INT,
    .libc_size_rank = MINIC_INTEGER_RANK_LONG,
};
static const MinicTargetSourceDescriptor minic_source_loongarch64 = {
    .source = {
        .asm_source_policy = &minic_loongarch64_asm_source_policy,
        .machine_builtin_policy = &minic_loongarch64_machine_builtin_policy,
        .alias_abi_policy = &minic_loongarch64_alias_abi_policy,
        .integer_model = &minic_lp64_signed_char_integer_model,
        .word_integer_rank = MINIC_INTEGER_RANK_LONG,
        .long_double_significand_bits = 113U,
        .language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS,
        .gnu_sizeof_void_is_one = true,
        .gnu_sizeof_function_is_one = true,
    },
    .wide_character_sign = MINIC_INTEGER_SIGN_SIGNED,
    .wide_character_rank = MINIC_INTEGER_RANK_INT,
    .libc_size_rank = MINIC_INTEGER_RANK_LONG,
};

/* Stable source registry keyed by the very same target catalog order.
 * C type identities are calculated once and never rebuilt by machine code. */
static const MinicTargetSourceDescriptor *const minic_target_source_descriptors[] = {
#define MINIC_TARGET_REGISTRATION(arch, id, spelling, stages, isa) \
    &minic_source_##id,
#include "target/target_registration.def"
#undef MINIC_TARGET_REGISTRATION
};

const struct MinicTargetSourceInfo *minic_target_source_info_for_architecture(
    MinicTargetArchitecture architecture) {
    static MinicTargetSourceInfo canonical_source_profiles[
        sizeof(minic_target_source_descriptors) /
        sizeof(minic_target_source_descriptors[0])];
    const MinicTargetCatalogEntry *catalog;
    size_t count;
    size_t index;

    catalog = minic_target_catalog_all(&count);
    if (count != sizeof(minic_target_source_descriptors) /
                 sizeof(minic_target_source_descriptors[0])) return NULL;
    for (index = 0U; index < count; ++index) {
        const MinicTargetSourceDescriptor *descriptor =
            minic_target_source_descriptors[index];
        MinicTargetSourceInfo *profile = &canonical_source_profiles[index];
        if (catalog[index].architecture != architecture) continue;
        if (profile->integer_model == NULL) {
            MinicTargetSourceInfo initialized = descriptor->source;
            if (initialized.integer_model == NULL ||
                !minic_target_source_integer_type_with_rank(
                    descriptor->wide_character_sign,
                    descriptor->wide_character_rank,
                    &initialized.wide_character_type) ||
                !minic_target_source_integer_type_with_rank(
                    MINIC_INTEGER_SIGN_UNSIGNED,
                    descriptor->libc_size_rank,
                    &initialized.libc_size_type))
                return NULL;
            *profile = initialized;
        }
        return profile;
    }
    return NULL;
}
