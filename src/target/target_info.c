#include "target/target_info.h"
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

/* This is the sole target-registration boundary for C language policy.
 * Backend ABI lowering is not inferred from shared integer widths or vararg
 * cursor representation: each target explicitly declares its own fields. */
typedef struct MinicTargetLanguageDescriptor {
    MinicTargetInfo target;
    const MinicDataLayout *(*layout)(void);
    MinicIntegerSign wide_character_sign;
    MinicIntegerRank wide_character_rank;
    MinicIntegerRank libc_size_rank;
} MinicTargetLanguageDescriptor;

static const MinicTargetLanguageDescriptor minic_language_riscv32 = {
        .target = {
            .asm_source_policy = &minic_riscv32_asm_source_policy,
            .integer_model = &minic_ilp32_unsigned_char_integer_model,
            .word_integer_rank = MINIC_INTEGER_RANK_INT,
            .gnu_sizeof_void_is_one = true,
            .gnu_sizeof_function_is_one = true,
            /* No RV64 cursor reuse: RV32 uses its own ILP32 register pairs. */
            .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
            .variadic_linear_slot_bytes = 4U,
            .variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
            .call_frame_return_address_level0 = true,
            .call_frame_frame_address_level0 = true,
            .riscv_isa = {.xlen = 32U, .abi = MINIC_RISCV_ABI_ILP32,
                          .strict_isa = true},
        },
        .layout = minic_riscv32_linux_data_layout,
        .wide_character_sign = MINIC_INTEGER_SIGN_SIGNED,
        .wide_character_rank = MINIC_INTEGER_RANK_INT,
        .libc_size_rank = MINIC_INTEGER_RANK_INT,
};
static const MinicTargetLanguageDescriptor minic_language_riscv64 = {
        .target = {
            .asm_source_policy = &minic_riscv64_asm_source_policy,
            .integer_model = &minic_lp64_unsigned_char_integer_model,
            .word_integer_rank = MINIC_INTEGER_RANK_LONG,
            .language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS,
            .gnu_sizeof_void_is_one = true,
            .gnu_sizeof_function_is_one = true,
            .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
            .variadic_linear_slot_bytes = 8U,
            .variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
            .pic_lowering_supported = true,
            .call_frame_return_address_level0 = true,
            .call_frame_frame_address_level0 = true,
            .riscv_isa = {.xlen = 64U, .abi = MINIC_RISCV_ABI_LP64D,
                          .strict_isa = false},
        },
        .layout = minic_default_data_layout,
        .wide_character_sign = MINIC_INTEGER_SIGN_SIGNED,
        .wide_character_rank = MINIC_INTEGER_RANK_INT,
        .libc_size_rank = MINIC_INTEGER_RANK_LONG,
};
static const MinicTargetLanguageDescriptor minic_language_x86_64 = {
        .target = {
            .asm_source_policy = &minic_x86_64_asm_source_policy,
            .integer_model = &minic_lp64_signed_char_integer_model,
            .word_integer_rank = MINIC_INTEGER_RANK_LONG,
            .language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS |
                MINIC_TARGET_LANG_ABI_STATE_VA_LIST_CURSOR |
                MINIC_TARGET_LANG_MS_X64_FUNCTION_POINTER_DECLARATIONS,
            .call_lowering_capabilities = MINIC_TARGET_CALL_MS_X64_FIXED_SCALAR,
            .gnu_sizeof_void_is_one = true,
            .gnu_sizeof_function_is_one = true,
            .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR,
            .variadic_copy_kind = MINIC_VARIADIC_COPY_ABI_STATE,
            .pic_lowering_supported = true,
            /* The SysV x86 rbp prologue supports only frame level zero. */
            .call_frame_return_address_level0 = true,
            .call_frame_frame_address_level0 = true,
        },
        .layout = minic_x86_64_linux_data_layout,
        .wide_character_sign = MINIC_INTEGER_SIGN_SIGNED,
        .wide_character_rank = MINIC_INTEGER_RANK_INT,
        .libc_size_rank = MINIC_INTEGER_RANK_LONG,
};
static const MinicTargetLanguageDescriptor minic_language_aarch64 = {
        .target = {
            .asm_source_policy = &minic_aarch64_asm_source_policy,
            .integer_model = &minic_lp64_unsigned_char_integer_model,
            .word_integer_rank = MINIC_INTEGER_RANK_LONG,
            .language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS |
                MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
            .gnu_sizeof_void_is_one = true,
            .gnu_sizeof_function_is_one = true,
            .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR,
            /* AAPCS64 va_copy duplicates an owned 32-byte ABI-state cursor. */
            .variadic_copy_kind = MINIC_VARIADIC_COPY_ABI_STATE,
            .pic_lowering_supported = true,
            .call_frame_return_address_level0 = true,
            .call_frame_frame_address_level0 = true,
            /* x29 can follow one AAPCS64 parent frame; other targets cannot. */
            .call_frame_frame_address_level1 = true,
        },
        .layout = minic_aarch64_linux_data_layout,
        .wide_character_sign = MINIC_INTEGER_SIGN_UNSIGNED,
        .wide_character_rank = MINIC_INTEGER_RANK_INT,
        .libc_size_rank = MINIC_INTEGER_RANK_LONG,
};
static const MinicTargetLanguageDescriptor minic_language_loongarch64 = {
        .target = {
            .asm_source_policy = &minic_loongarch64_asm_source_policy,
            .machine_builtin_policy = &minic_loongarch64_machine_builtin_policy,
            .integer_model = &minic_lp64_signed_char_integer_model,
            .word_integer_rank = MINIC_INTEGER_RANK_LONG,
            .language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS,
            .gnu_sizeof_void_is_one = true,
            .gnu_sizeof_function_is_one = true,
            .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
            .variadic_linear_slot_bytes = 8U,
            .variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
            .pic_lowering_supported = true,
            .call_frame_return_address_level0 = true,
            .call_frame_frame_address_level0 = true,
        },
        .layout = minic_loongarch64_linux_data_layout,
        .wide_character_sign = MINIC_INTEGER_SIGN_SIGNED,
        .wide_character_rank = MINIC_INTEGER_RANK_INT,
        .libc_size_rank = MINIC_INTEGER_RANK_LONG,
};

/* The catalog, language descriptor, and backend indexes are generated from
 * one registration inventory. Architecture IDs are assigned by the catalog,
 * never independently copied into the C language/ABI facts above. */
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
            initialized.architecture = catalog[index].architecture;
            initialized.data_layout = descriptor->layout();
            if (initialized.data_layout == NULL ||
                !integer_type_with_rank(descriptor->wide_character_sign,
                                        descriptor->wide_character_rank,
                                        &initialized.wide_character_type) ||
                !integer_type_with_rank(MINIC_INTEGER_SIGN_UNSIGNED,
                                        descriptor->libc_size_rank,
                                        &initialized.libc_size_type))
                return NULL;
            *target = initialized;
        }
        return target;
    }
    return NULL;
}

/* Target C language/ABI state is a translation-unit property, not a global
 * CPU property. This owns the option semantics and preserves defaults when a
 * -fno-* spelling is explicitly present. */
bool minic_target_invocation_begin(MinicTargetInvocation *invocation,
                                   const MinicTargetInfo *canonical) {
    if (invocation == NULL || canonical == NULL ||
        canonical->integer_model == NULL || canonical->data_layout == NULL)
        return false;
    invocation->target = *canonical;
    invocation->integer_model = *canonical->integer_model;
    invocation->target.integer_model = &invocation->integer_model;
    invocation->default_wide_character_type = canonical->wide_character_type;
    return true;
}

bool minic_target_invocation_plain_char(MinicTargetInvocation *invocation,
                                        MinicIntegerSign sign) {
    if (invocation == NULL ||
        (sign != MINIC_INTEGER_SIGN_SIGNED &&
         sign != MINIC_INTEGER_SIGN_UNSIGNED))
        return false;
    invocation->integer_model.plain_char_sign = sign;
    return true;
}

bool minic_target_invocation_short_wchar(MinicTargetInvocation *invocation,
                                         bool enabled) {
    if (invocation == NULL) return false;
    invocation->target.wide_character_type = enabled
        ? minic_type_unsigned_short()
        : invocation->default_wide_character_type;
    /* -fno-short-wchar restores the canonical descriptor chosen by begin().
     * A target that defaults to 16-bit wchar must not suddenly become int. */
    return true;
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

bool minic_target_info_header_opaque_vector_return(const MinicTargetInfo *target,
                                                    MinicType return_type) {
    if (return_type.pointer_depth != 0U ||
        !minic_target_info_language_type_supported(target, return_type.base_kind))
        return false;
    switch (return_type.base_kind) {
    case MINIC_TYPE_BASE_AARCH64_FLOAT32X4:
    case MINIC_TYPE_BASE_AARCH64_FLOAT64X2:
    case MINIC_TYPE_BASE_AARCH64_SVE_FLOAT32:
    case MINIC_TYPE_BASE_AARCH64_SVE_FLOAT64:
    case MINIC_TYPE_BASE_AARCH64_SVE_BOOL:
        return true;
    default:
        return false;
    }
}

/* A glibc header-only PCS marker must not enable actual vector call ABI.
 * Naming/target admission is centralized here, not in the C parser. */
bool minic_target_info_header_attribute_supported(const MinicTargetInfo *target,
                                                  const char *spelling,
                                                  size_t length) {
    static const char name[] = "__aarch64_vector_pcs__";
    return target != NULL && spelling != NULL &&
           (target->language_type_capabilities &
            MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS) != 0U &&
           length == sizeof(name) - 1U &&
           memcmp(spelling, name, length) == 0;
}

/* The GNU function annotations emitted by native x86 Linux headers/objects
 * are declarations of the existing x86 function ABI, not a request to share
 * x86 names with foreign targets. Parser-owned argument validation is needed
 * before using these annotations; this function handles only admission. */
bool minic_target_info_gnu_function_security_attribute_supported(
    const MinicTargetInfo *target, const char *attribute, size_t length) {
    static const char *const names[] = {
        "nocf_check", "__indirect_branch__", "__target__"
    };
    size_t index;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        attribute == NULL) {
        return false;
    }
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (length == strlen(names[index]) &&
            memcmp(attribute, names[index], length) == 0) {
            return true;
        }
    }
    return false;
}

/* Both AArch64's ADRP+ADD and LoongArch's la.local are valid in PIE only
 * when a symbol is nonpreemptible. Never infer internal linkage from the
 * spelling alone or from a source declaration without an owned definition. */
bool minic_target_info_elf_object_is_tu_local(const MinicC0Program *program,
                                              const char *symbol) {
    size_t index;
    if (program == NULL || symbol == NULL) return false;
    for (index = 0U; index < program->global_object_count; ++index) {
        const MinicGlobalObject *object = &program->global_objects[index];
        if (object->name != NULL && strcmp(object->name, symbol) == 0 &&
            object->is_internal && !object->is_extern &&
            !object->is_block_scope_extern_only) return true;
    }
    return false;
}

bool minic_target_info_elf_function_is_tu_local(const MinicC0Program *program,
                                                const char *symbol) {
    size_t index;
    if (program == NULL || symbol == NULL) return false;
    for (index = 0U; index < program->function_count; ++index) {
        const MinicFunction *function = &program->functions[index];
        /* TargetInfo is also linked by standalone ABI/layout tests without
         * the compiler's C0 symbol helper; keep this lookup self-contained. */
        const char *name = function->assembler_name != NULL
            ? function->assembler_name : function->name;
        if (name != NULL && strcmp(name, symbol) == 0 &&
            function->is_defined && function->is_internal) return true;
    }
    return false;
}

/* ABI type spelling is a language capability; the target backend still
 * decides separately which calling conventions it can actually emit. */
bool minic_target_info_function_pointer_calling_convention_declaration_supported(
    const MinicTargetInfo *target, MinicFunctionCallingConvention convention) {
    if (target == NULL) return false;
    if (convention == MINIC_CALLING_CONVENTION_DEFAULT) return true;
    return convention == MINIC_CALLING_CONVENTION_MS_X64 &&
           (target->language_type_capabilities &
            MINIC_TARGET_LANG_MS_X64_FUNCTION_POINTER_DECLARATIONS) != 0U;
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

/* For LoongArch's Linux syscall `sys_* -> __se_sys_*` aliases the public
 * ABI names typed ints/pointers while the sanitizer entry receives one
 * 64-bit word per argument. ABI transport is the identical GPR slot, NOT a
 * C type cast. Never admit floating/aggregate, 128-bit, variadic, or
 * mismatched return signatures using this special admission. */
static bool minic_loongarch64_syscall_alias_word_pair(
    const MinicTargetInfo *target, const MinicC0Program *program,
    MinicType call_type, MinicType definition_type) {
    size_t call_size, definition_size;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        program == NULL ||
        (!minic_type_is_integer(call_type) && !minic_type_is_pointer(call_type)) ||
        !minic_type_is_integer(definition_type) ||
        !minic_target_info_sizeof_type(target, program, call_type, &call_size) ||
        !minic_target_info_sizeof_type(target, program, definition_type, &definition_size))
        return false;
    /* The sanitizer must receive full-width integer ABI words; allowing an
     * arbitrary 32-bit callee could truncate an address or signed input. */
    return call_size != 0U && call_size <= 8U && definition_size == 8U;
}

bool minic_target_info_function_alias_abi_compatible(
    const MinicTargetInfo *target, const MinicC0Program *program,
    const MinicFunction *alias, const MinicFunction *owner) {
    size_t i;
    if (target == NULL || program == NULL || alias == NULL || owner == NULL ||
        alias->parameter_count != owner->parameter_count ||
        alias->is_variadic != owner->is_variadic ||
        !minic_c0_types_compatible(
            program, alias->return_type, owner->return_type))
        return false;
    for (i = 0U; i < alias->parameter_count; ++i) {
        if (minic_c0_types_compatible(
                program, alias->parameter_types[i], owner->parameter_types[i]))
            continue;
        if (alias->is_variadic || alias->is_internal || owner->is_internal ||
            !minic_loongarch64_syscall_alias_word_pair(
                target, program, alias->parameter_types[i],
                owner->parameter_types[i]))
            return false;
    }
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

/* The language boundary is intentionally policy-agnostic: a new target
 * registers one callback descriptor, without adding target-specific checks to
 * Frontend, Core, or these GNU source admission functions. Native machine
 * operand allocation and final emitter qualification are separate contracts. */
static const MinicTargetAsmSourcePolicy *minic_target_source_policy(
    const MinicTargetInfo *target) {
    return target == NULL ? NULL : target->asm_source_policy;
}

bool minic_target_info_fixed_register_supported(const MinicTargetInfo *target,
                                                const char *name,
                                                size_t name_length) {
    const MinicTargetAsmSourcePolicy *policy = minic_target_source_policy(target);
    return policy != NULL && name != NULL && policy->fixed_register_read != NULL &&
           policy->fixed_register_read(name, name_length);
}

bool minic_target_info_fixed_register_write_supported(
    const MinicTargetInfo *target, const char *name, size_t length) {
    const MinicTargetAsmSourcePolicy *policy = minic_target_source_policy(target);
    return policy != NULL && name != NULL && policy->fixed_register_write != NULL &&
           policy->fixed_register_write(name, length);
}

bool minic_target_info_local_fixed_register_supported(
    const MinicTargetInfo *target, const char *name, size_t name_length) {
    const MinicTargetAsmSourcePolicy *policy = minic_target_source_policy(target);
    return policy != NULL && name != NULL && policy->local_fixed_register != NULL &&
           policy->local_fixed_register(name, name_length);
}

bool minic_target_info_inline_asm_readwrite_hardreg_supported(
    const MinicTargetInfo *target, const char *reg, size_t reg_length,
    const char *constraint, size_t length, MinicType operand_type) {
    const MinicTargetAsmSourcePolicy *policy = minic_target_source_policy(target);
    return policy != NULL && reg != NULL && constraint != NULL &&
           policy->readwrite_hardreg != NULL &&
           policy->readwrite_hardreg(reg, reg_length, constraint, length, operand_type);
}

bool minic_target_info_inline_asm_register_clobber_supported(
    const MinicTargetInfo *target, const char *name, size_t name_length) {
    const MinicTargetAsmSourcePolicy *policy = minic_target_source_policy(target);
    return policy != NULL && name != NULL && policy->register_clobber != NULL &&
           policy->register_clobber(name, name_length);
}

bool minic_target_info_inline_asm_source_clobber_supported(
    const MinicTargetInfo *target, const char *name, size_t length) {
    const MinicTargetAsmSourcePolicy *policy = minic_target_source_policy(target);
    if (policy == NULL || name == NULL) return false;
    if (policy->source_clobber != NULL)
        return policy->source_clobber(name, length);
    return policy->register_clobber != NULL &&
           policy->register_clobber(name, length);
}

bool minic_target_info_machine_builtin_signature(
    const MinicTargetInfo *target, const char *spelling, size_t length,
    char *callee, size_t callee_capacity, MinicType *result,
    MinicType arguments[3], size_t *argument_count,
    const char **mnemonic, size_t *immediate_index) {
    const MinicTargetMachineBuiltinPolicy *policy =
        target == NULL ? NULL : target->machine_builtin_policy;
    return policy != NULL && policy->signature != NULL &&
           policy->signature(target, spelling, length, callee, callee_capacity,
                             result, arguments, argument_count, mnemonic,
                             immediate_index);
}

bool minic_target_info_inline_asm_memory_input_constraint_supported(
    const MinicTargetInfo *target, const char *constraint,
    size_t constraint_length) {
    const MinicTargetAsmSourcePolicy *policy = minic_target_source_policy(target);
    if (policy == NULL || constraint == NULL || constraint_length != 1U)
        return false;
    /* m is universal GNU memory source syntax. ISA-specific alternatives
     * (for example AArch64 Q) are admitted by the registered target owner. */
    return constraint[0] == 'm' ||
           (policy->memory_input_constraint != NULL &&
            policy->memory_input_constraint(constraint, constraint_length));
}

bool minic_target_info_inline_asm_immediate_constraint_supported(
    const MinicTargetInfo *target, const char *constraint,
    size_t constraint_length, int64_t value) {
    const MinicTargetAsmSourcePolicy *policy = minic_target_source_policy(target);
    return policy != NULL && constraint != NULL &&
           policy->immediate_constraint != NULL &&
           policy->immediate_constraint(constraint, constraint_length, value);
}

bool minic_target_info_inline_asm_immediate_may_defer(
    const MinicTargetInfo *target) {
    const MinicTargetAsmSourcePolicy *policy = minic_target_source_policy(target);
    return policy != NULL && policy->immediate_may_defer;
}

const char *minic_target_info_inline_asm_immediate_constraint_diagnostic(
    const MinicTargetInfo *target) {
    const MinicTargetAsmSourcePolicy *policy = minic_target_source_policy(target);
    return policy != NULL && policy->immediate_diagnostic != NULL
        ? policy->immediate_diagnostic
        : "GNU asm 'I' input requires a signed 12-bit integer constant";
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
    case MINIC_TARGET_ARCH_AARCH64: {
        MinicType pointee;
        /* Only MiniC's canonical void* builtin proxy is a qualified
         * AAPCS64 cursor; an arbitrary source pointer must not acquire
         * a hidden 32-byte copy ABI by accident. */
        return minic_type_pointee(type, &pointee) &&
               minic_type_is_void(pointee) &&
               !minic_type_is_const(type) &&
               !minic_type_is_volatile(type);
    }
    default:
        return false;
    }
}
