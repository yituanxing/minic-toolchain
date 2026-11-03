#include "target/target_info.h"
#include "core/module_ir.h"
#include "target/loongarch64/machine_builtin_form.h"
#include "target/source_policy/source_policy.h"

#include <stdint.h>
#include <string.h>

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

/* One catalog owns machine capability and a distinct immutable C source
 * profile; the invocation constructs a writable copy of both. */
typedef struct MinicTargetLanguageDescriptor {
    MinicTargetInfo target;
    MinicTargetSourceInfo source;
    const MinicDataLayout *(*layout)(void);
    MinicIntegerSign wide_character_sign;
    MinicIntegerRank wide_character_rank;
    MinicIntegerRank libc_size_rank;
} MinicTargetLanguageDescriptor;

static const MinicTargetLanguageDescriptor minic_language_riscv32 = {
    .target = {
        .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
        .variadic_linear_slot_bytes = 4U,
        .variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
        .pic_lowering_supported = true,
        .call_frame_return_address_level0 = true,
        .call_frame_frame_address_level0 = true,
        .riscv_isa = {.xlen = 32U, .abi = MINIC_RISCV_ABI_ILP32,
                      .strict_isa = true},
    },
    .source = {
        .asm_source_policy = &minic_riscv32_asm_source_policy,
        .integer_model = &minic_ilp32_unsigned_char_integer_model,
        .word_integer_rank = MINIC_INTEGER_RANK_INT,
        .long_double_significand_bits = 113U,
        .gnu_sizeof_void_is_one = true,
        .gnu_sizeof_function_is_one = true,
    },
    .layout = minic_riscv32_linux_data_layout,
    .wide_character_sign = MINIC_INTEGER_SIGN_SIGNED,
    .wide_character_rank = MINIC_INTEGER_RANK_INT,
    .libc_size_rank = MINIC_INTEGER_RANK_INT,
};
static const MinicTargetLanguageDescriptor minic_language_riscv64 = {
    .target = {
        .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
        .variadic_linear_slot_bytes = 8U,
        .variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
        .pic_lowering_supported = true,
        .call_frame_return_address_level0 = true,
        .call_frame_frame_address_level0 = true,
        .riscv_isa = {.xlen = 64U, .abi = MINIC_RISCV_ABI_LP64D,
                      .strict_isa = false},
    },
    .source = {
        .asm_source_policy = &minic_riscv64_asm_source_policy,
        .integer_model = &minic_lp64_unsigned_char_integer_model,
        .word_integer_rank = MINIC_INTEGER_RANK_LONG,
        .long_double_significand_bits = 113U,
        .language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS,
        .gnu_sizeof_void_is_one = true,
        .gnu_sizeof_function_is_one = true,
    },
    .layout = minic_default_data_layout,
    .wide_character_sign = MINIC_INTEGER_SIGN_SIGNED,
    .wide_character_rank = MINIC_INTEGER_RANK_INT,
    .libc_size_rank = MINIC_INTEGER_RANK_LONG,
};
static const MinicTargetLanguageDescriptor minic_language_x86_64 = {
    .target = {
        .call_lowering_capabilities = MINIC_TARGET_CALL_MS_X64_FIXED_SCALAR,
        .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR,
        .variadic_copy_kind = MINIC_VARIADIC_COPY_ABI_STATE,
        .pic_lowering_supported = true,
        .call_frame_return_address_level0 = true,
        .call_frame_frame_address_level0 = true,
    },
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
    .layout = minic_x86_64_linux_data_layout,
    .wide_character_sign = MINIC_INTEGER_SIGN_SIGNED,
    .wide_character_rank = MINIC_INTEGER_RANK_INT,
    .libc_size_rank = MINIC_INTEGER_RANK_LONG,
};
static const MinicTargetLanguageDescriptor minic_language_aarch64 = {
    .target = {
        .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR,
        .variadic_copy_kind = MINIC_VARIADIC_COPY_ABI_STATE,
        .pic_lowering_supported = true,
        .call_frame_return_address_level0 = true,
        .call_frame_frame_address_level0 = true,
        .call_frame_frame_address_level1 = true,
    },
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
    .layout = minic_aarch64_linux_data_layout,
    .wide_character_sign = MINIC_INTEGER_SIGN_UNSIGNED,
    .wide_character_rank = MINIC_INTEGER_RANK_INT,
    .libc_size_rank = MINIC_INTEGER_RANK_LONG,
};
static const MinicTargetLanguageDescriptor minic_language_loongarch64 = {
    .target = {
        .machine_builtin_form_lookup = minic_loongarch64_machine_builtin_form,
        .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
        .variadic_linear_slot_bytes = 8U,
        .variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
        .pic_lowering_supported = true,
        .call_frame_return_address_level0 = true,
        .call_frame_frame_address_level0 = true,
    },
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
    .layout = minic_loongarch64_linux_data_layout,
    .wide_character_sign = MINIC_INTEGER_SIGN_SIGNED,
    .wide_character_rank = MINIC_INTEGER_RANK_INT,
    .libc_size_rank = MINIC_INTEGER_RANK_LONG,
};

/* The catalog is the sole target registration order, shared by the driver,
 * source ABI profiles and five machine backends. */
static const MinicTargetLanguageDescriptor *const minic_target_language_descriptors[] = {
#define MINIC_TARGET_REGISTRATION(arch, id, spelling, stages, isa) \
    &minic_language_##id,
#include "target/target_registration.def"
#undef MINIC_TARGET_REGISTRATION
};

const MinicTargetInfo *minic_target_info_for_architecture(
    MinicTargetArchitecture architecture) {
    static MinicTargetInfo canonical_targets[
        sizeof(minic_target_language_descriptors) /
        sizeof(minic_target_language_descriptors[0])];
    static MinicTargetSourceInfo canonical_source_profiles[
        sizeof(minic_target_language_descriptors) /
        sizeof(minic_target_language_descriptors[0])];
    const MinicTargetCatalogEntry *catalog;
    size_t count;
    size_t index;

    catalog = minic_target_catalog_all(&count);
    if (count != sizeof(minic_target_language_descriptors) /
                 sizeof(minic_target_language_descriptors[0])) return NULL;
    for (index = 0U; index < count; ++index) {
        const MinicTargetLanguageDescriptor *descriptor = minic_target_language_descriptors[index];
        MinicTargetInfo *target = &canonical_targets[index];
        if (catalog[index].architecture != architecture) continue;
        if (target->data_layout == NULL) {
            MinicTargetInfo initialized = descriptor->target;
            MinicTargetSourceInfo source = descriptor->source;
            initialized.architecture = catalog[index].architecture;
            initialized.data_layout = descriptor->layout();
            if (initialized.data_layout == NULL ||
                !minic_target_source_integer_type_with_rank(descriptor->wide_character_sign,
                                        descriptor->wide_character_rank,
                                        &source.wide_character_type) ||
                !minic_target_source_integer_type_with_rank(MINIC_INTEGER_SIGN_UNSIGNED,
                                        descriptor->libc_size_rank,
                                        &source.libc_size_type))
                return NULL;
            canonical_source_profiles[index] = source;
            initialized.source_info = &canonical_source_profiles[index];
            *target = initialized;
        }
        return target;
    }
    return NULL;
}

const MinicTargetInfo *minic_default_target_info(void) {
    return minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64);
}

/* Target spellings are owned by one catalog shared with the staged driver.
 * TargetInfo remains the authority for C widths, language facts and ABI. */
const MinicTargetInfo *minic_target_info_from_name(const char *name) {
    const MinicTargetCatalogEntry *entry = minic_target_catalog_find(name);
    return entry == NULL ? NULL :
           minic_target_info_for_architecture(entry->architecture);
}

const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target) {
    return target == NULL ? NULL : target->data_layout;
}

static bool minic_elf_module_visibility_is_nonpreemptible(
    MinicCoreModuleVisibility visibility) {
    return visibility == MINIC_CORE_MODULE_VISIBILITY_HIDDEN ||
           visibility == MINIC_CORE_MODULE_VISIBILITY_INTERNAL;
}

bool minic_target_info_elf_module_object_is_tu_local(
    const MinicCoreModule *module, const char *symbol) {
    size_t index;
    if (module == NULL || !module->finalized || symbol == NULL ||
        (module->global_count != 0U && module->globals == NULL)) return false;
    for (index = 0U; index < module->global_count; ++index) {
        const MinicCoreModuleGlobal *object = &module->globals[index];
        if (object->name != NULL && strcmp(object->name, symbol) == 0 &&
            object->is_internal && !object->is_extern &&
            !object->is_block_scope_extern_only) return true;
    }
    return false;
}

bool minic_target_info_elf_module_function_is_tu_local(
    const MinicCoreModule *module, const char *symbol) {
    size_t index;
    if (module == NULL || !module->finalized || symbol == NULL ||
        (module->function_count != 0U && module->function_symbols == NULL)) return false;
    for (index = 0U; index < module->function_count; ++index) {
        const MinicCoreModuleFunctionSymbol *function =
            &module->function_symbols[index];
        if (function->name != NULL && strcmp(function->name, symbol) == 0 &&
            function->is_defined && function->is_internal) return true;
    }
    return false;
}

bool minic_target_info_elf_module_object_is_nonpreemptible(
    const MinicCoreModule *module, const char *symbol) {
    size_t index;
    if (module == NULL || !module->finalized || symbol == NULL ||
        (module->global_count != 0U && module->globals == NULL)) return false;
    for (index = 0U; index < module->global_count; ++index) {
        const MinicCoreModuleGlobal *object = &module->globals[index];
        if (object->name != NULL && strcmp(object->name, symbol) == 0 &&
            ((object->is_internal && !object->is_extern &&
              !object->is_block_scope_extern_only) ||
             minic_elf_module_visibility_is_nonpreemptible(object->visibility)))
            return true;
    }
    return false;
}

bool minic_target_info_elf_module_function_is_nonpreemptible(
    const MinicCoreModule *module, const char *symbol) {
    size_t index;
    if (module == NULL || !module->finalized || symbol == NULL ||
        (module->function_count != 0U && module->function_symbols == NULL)) return false;
    for (index = 0U; index < module->function_count; ++index) {
        const MinicCoreModuleFunctionSymbol *function =
            &module->function_symbols[index];
        if (function->name != NULL && strcmp(function->name, symbol) == 0 &&
            ((function->is_defined && function->is_internal) ||
             minic_elf_module_visibility_is_nonpreemptible(function->visibility)))
            return true;
    }
    return false;
}

bool minic_target_info_elf_module_object_is_undefined_weak(
    const MinicCoreModule *module, const char *symbol) {
    size_t index;
    if (module == NULL || !module->finalized || symbol == NULL ||
        (module->global_count != 0U && module->globals == NULL)) return false;
    for (index = 0U; index < module->global_count; ++index) {
        const MinicCoreModuleGlobal *object = &module->globals[index];
        if (object->name != NULL && strcmp(object->name, symbol) == 0)
            return object->is_weak && object->is_extern &&
                   !object->is_tentative;
    }
    return false;
}

bool minic_target_info_elf_module_function_is_undefined_weak(
    const MinicCoreModule *module, const char *symbol) {
    size_t index;
    if (module == NULL || !module->finalized || symbol == NULL ||
        (module->function_count != 0U && module->function_symbols == NULL)) return false;
    for (index = 0U; index < module->function_count; ++index) {
        const MinicCoreModuleFunctionSymbol *function =
            &module->function_symbols[index];
        if (function->name != NULL && strcmp(function->name, symbol) == 0)
            return function->is_weak && !function->is_defined;
    }
    return false;
}

bool minic_target_info_function_calling_convention_scalar_call_supported(
    const MinicTargetInfo *target, MinicFunctionCallingConvention convention) {
    if (target == NULL || !minic_function_calling_convention_valid(convention))
        return false;
    if (convention == MINIC_CALLING_CONVENTION_DEFAULT)
        return true;
    return convention == MINIC_CALLING_CONVENTION_MS_X64 &&
           (target->call_lowering_capabilities &
            MINIC_TARGET_CALL_MS_X64_FIXED_SCALAR) != 0U;
}
bool minic_target_info_call_frame_address_supported(const MinicTargetInfo *target,
                                                    MinicCallFrameAddressKind kind,
                                                    unsigned int level) {
    if (target == NULL || level > 1U) {
        return false;
    }
    switch (kind) {
    case MINIC_CALL_FRAME_ADDRESS_RETURN:
        return level == 0U && target->call_frame_return_address_level0;
    case MINIC_CALL_FRAME_ADDRESS_FRAME:
        return level == 0U ? target->call_frame_frame_address_level0 :
                             target->call_frame_frame_address_level1;
    }
    return false;
}

bool minic_target_info_machine_builtin_form(
    const MinicTargetInfo *target, const char *internal_callee, size_t length,
    size_t *argument_count,
    const char **mnemonic, size_t *immediate_index) {
    return target != NULL && target->machine_builtin_form_lookup != NULL &&
           target->machine_builtin_form_lookup(
               target, internal_callee, length, argument_count,
               mnemonic, immediate_index);
}
