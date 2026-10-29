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
            rv64_target.pic_lowering_supported = true;
            rv64_target.riscv_isa.xlen = 64U;
            rv64_target.riscv_isa.abi = MINIC_RISCV_ABI_LP64D;
            rv64_target.riscv_isa.strict_isa = false;
            rv64_target.data_layout = minic_default_data_layout();
            rv64_target.integer_model = &minic_rv64_integer_model;
            /* RISC-V Linux psABI: wchar_t is signed 32-bit on ILP32 and LP64.
             * -fshort-wchar remains a per-invocation 16-bit override. */
            rv64_target.wide_character_type = minic_type_int();
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
            x86_64_target.pic_lowering_supported = true;
            x86_64_target.data_layout = minic_x86_64_linux_data_layout();
            x86_64_target.integer_model = &minic_x86_64_linux_integer_model;
            x86_64_target.wide_character_type = minic_type_int();
            x86_64_target.libc_size_type = minic_type_unsigned_long();
            x86_64_target.word_integer_rank = MINIC_INTEGER_RANK_LONG;
            x86_64_target.language_type_capabilities = MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS | MINIC_TARGET_LANG_ABI_STATE_VA_LIST_CURSOR | MINIC_TARGET_LANG_MS_X64_FUNCTION_POINTER_DECLARATIONS;
            x86_64_target.call_lowering_capabilities =
                MINIC_TARGET_CALL_MS_X64_FIXED_SCALAR;
            x86_64_target.gnu_sizeof_void_is_one = true;
            x86_64_target.gnu_sizeof_function_is_one = true;
            x86_64_target.variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR;
            x86_64_target.variadic_linear_slot_bytes = 0U;
            x86_64_target.variadic_copy_kind = MINIC_VARIADIC_COPY_ABI_STATE;
            /* Every SysV x86 function begins with push rbp/mov rsp,rbp.
             * Hence true entry return-PC is 8(rbp), and frame level0 is
             * rbp even after nested CALL, stack alignment or alloca.
             * No cross-function unwind or nonzero level is admitted. */
            x86_64_target.call_frame_return_address_level0 = true;
            x86_64_target.call_frame_frame_address_level0 = true;
        }
        return &x86_64_target;
    }
    if (architecture == MINIC_TARGET_ARCH_AARCH64) {
        if (aarch64_target.data_layout == NULL) {
            aarch64_target.architecture = MINIC_TARGET_ARCH_AARCH64;
            /* The AArch64 emitter resolves TU-local addresses with
             * ADRP+LO12 and interposable addresses via GOT page loads.
             * BL relocations remain PLT-capable, so both PIE and DSO code
             * use the same position-independent address lowering. */
            aarch64_target.pic_lowering_supported = true;
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
            /* AAPCS64 carries a mutable 32-byte ABI-state record behind
             * the frontend's pointer proxy. Core va_copy requests an owned
             * destination cursor, never a scalar pointer assignment. */
            aarch64_target.variadic_copy_kind = MINIC_VARIADIC_COPY_ABI_STATE;
            /* Native AAPCS64 owns the x29 chain [caller-fp, caller-lr]
             * including leaf and dynamic-stack functions. Return PC level
             * zero is separately saved as the entry LR. */
            aarch64_target.call_frame_return_address_level0 = true;
            aarch64_target.call_frame_frame_address_level0 = true;
            aarch64_target.call_frame_frame_address_level1 = true;
        }
        return &aarch64_target;
    }
    if (architecture == MINIC_TARGET_ARCH_LOONGARCH64) {
        if (loongarch64_target.data_layout == NULL) {
            loongarch64_target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
            /* la.local emits PC-relative TU-local addresses, whereas
             * la.global emits GOT-based references to preemptible symbols.
             * The target owns both classifications and B26/PLT calls. */
            loongarch64_target.pic_lowering_supported = true;
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
            /* GNU __builtin_return_address(0) reads the ENTRY ra. This is
             * independent of whether the function later issues direct or
             * indirect calls: the shared LP64 CALL plan saves ra at entry
             * whenever it can be clobbered. No level>0 unwind or frame
             * address is implied by this capability. */
            loongarch64_target.call_frame_return_address_level0 = true;
            /* Original LoongArch Linux stacktrace/process use level-0 SP:
             * only a stable current frame base, never parent unwinding. */
            loongarch64_target.call_frame_frame_address_level0 = true;
        }
        return &loongarch64_target;
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

bool minic_target_info_fixed_register_supported(const MinicTargetInfo *target,
                                                const char *name,
                                                size_t name_length) {
    if (target == NULL || name == NULL) return false;
    /* Linux x86's current_stack_pointer is a read of the LIVE rsp, not
     * a load from an ELF object and not an alias for rbp. The machine
     * emitter now owns the matching fixed-register Core instruction.
     * Do not allow rbp, scratch, or arbitrary SysV registers without
     * an independent assignment and preservation contract. */
    if (target->architecture == MINIC_TARGET_ARCH_X86_64)
        return name_length == 3U && memcmp(name, "rsp", 3U) == 0;
    /* Original GNU Linux AArch64 also defines current_stack_pointer as a
     * file-scope register asm("sp").  AArch64 now consumes the same shared
     * Core fixed-register-read contract as x86, but emits its own SP opcode
     * and preserves the AAPCS64 frame.  x18/x19 and other GPRs are not
     * automatically qualified. */
    if (target->architecture == MINIC_TARGET_ARCH_AARCH64)
        return name_length == 2U && memcmp(name, "sp", 2U) == 0;
    /* Original Linux 6.6.143's LoongArch global variables bind TP, SP and
     * $r21 (per-CPU offset). Kernel ABI reserves r21, so it is safe to READ
     * these architectural registers as live Core values. Never admit writes
     * or caller/scratch bindings here. */
    if (target->architecture == MINIC_TARGET_ARCH_LOONGARCH64)
        return (name_length == 3U &&
                (memcmp(name, "$tp", 3U) == 0 ||
                 memcmp(name, "$sp", 3U) == 0)) ||
               (name_length == 4U && memcmp(name, "$r21", 4U) == 0);
    if (target->architecture != MINIC_TARGET_ARCH_RISCV64 &&
        target->architecture != MINIC_TARGET_ARCH_RISCV32) return false;
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

bool minic_target_info_inline_asm_readwrite_hardreg_supported(
    const MinicTargetInfo *target, const char *reg, size_t reg_length,
    const char *constraint, size_t length, MinicType operand_type) {
    /* Original Linux 6.6 x86 current_stack_pointer asm("rsp") is a
     * GNU extended-asm tied +r operand. C assignment to RSP is NOT
     * permitted and Core must separately validate the whole machine
     * template, physical stack effects, registers and clobber set. */
    return target != NULL && reg != NULL && constraint != NULL &&
           target->architecture == MINIC_TARGET_ARCH_X86_64 &&
           reg_length == 3U && memcmp(reg, "rsp", 3U) == 0 &&
           length == 2U && memcmp(constraint, "+r", 2U) == 0 &&
           minic_type_equal(operand_type, minic_type_unsigned_long());
}

bool minic_target_info_fixed_register_write_supported(
    const MinicTargetInfo *target, const char *name, size_t length) {
    /* Linux LoongArch relocates its initial thread_info through the reserved
     * global register asm("$tp") alias. This is a genuine live TP assignment,
     * not an ordinary ELF symbol store. The independent per-CPU $r21 alias
     * remains writable. SP and all compiler temporary registers are not. */
    return target != NULL && name != NULL &&
           target->architecture == MINIC_TARGET_ARCH_LOONGARCH64 &&
           ((length == 4U && memcmp(name, "$r21", 4U) == 0) ||
            (length == 3U && memcmp(name, "$tp", 3U) == 0));
}

bool minic_target_info_local_fixed_register_supported(const MinicTargetInfo *target,
                                                      const char *name,
                                                      size_t name_length) {
    if (target == NULL || name == NULL) return false;
    if (target->architecture == MINIC_TARGET_ARCH_LOONGARCH64) {
        /* These are the ONLY hard-register local names in the original
         * 994-file GNU Linux 6.6.143 Kbuild source census. Local bindings
         * are C object declarations, not unconditional machine operands.
         * Structured GNU asm must independently qualify the physical
         * binding/constraint/clobber; the Loong machine emitter currently
         * rejects unqualified asm instead of silently discarding a binding.
         * In particular, ordinary t1..t8 scratch locals are NOT admitted. */
        return (name_length == 2U &&
                ((name[0] == 't' && name[1] == '0') ||
                 (name[0] == 'a' &&
                  (name[1] == '0' || name[1] == '1' || name[1] == '7'))));
    }
    if (target->architecture == MINIC_TARGET_ARCH_X86_64) {
        /* Linux put_user() binds local hard registers using asm("%""rax"),
         * asm("%""rdx"), etc. GNU treats the leading '%' as an AT&T
         * register-name marker, not a distinct hardware register. Normalize
         * ONLY for target local-declaration admission; the structured ASM
         * machine owner remains responsible for physical binding, clobbers
         * and stack ABI. Do not permit a lone '%', special registers, saved
         * registers, or x86 syntax to leak into the Common Core. */
        if (name_length > 1U && name[0] == '%') {
            ++name;
            --name_length;
        }
        /* Source GNU local-register variables are not register
         * reservations. Their only guaranteed machine effect is when
         * referenced as qualified GNU extended-ASM operands. Reuse the
         * existing SysV caller-clobber register grammar, excluding cc,
         * while the x86 machine ASM owner independently checks physical
         * constraints before emission. Callee-saved registers stay blocked
         * until the backend explicitly proves save/restore preservation. */
        return !(name_length == 2U && memcmp(name, "cc", 2U) == 0) &&
               minic_target_info_inline_asm_register_clobber_supported(
                   target, name, name_length);
    }
    if (target->architecture == MINIC_TARGET_ARCH_AARCH64) {
        /* Linux 6.6 current_stack_pointer is declared as a fixed `sp`
         * register alias. The AArch64 machine owner already implements
         * exactly its READ as `mov x9, sp`; no writes, stack adjustments,
         * or arbitrary hard-register reservations are implied here. */
        if (name_length == 2U && memcmp(name, "sp", 2U) == 0)
            return true;
        /* AAPCS64 x0..x17 are the integer argument and caller-scratch
         * GPR names used by original GNU Linux C (e.g. syscall x8).
         * GNU arm64 SMCCC also spells the same physical register r0..r17
         * in local `register ... asm("r0")` declarations. Admitting a local
         * variable does NOT reserve that register: the complete structured
         * SMC/HVC operand and clobber contract must still be proven by the
         * AArch64 machine owner before emitting the privileged instruction.
         * Keep platform-reserved x18, stack pointer, and callee-saved
         * x19..x29 excluded until machine ownership is certified. */
        unsigned int number;
        if (name_length < 2U || name_length > 3U ||
            (name[0] != 'x' && name[0] != 'r') ||
            name[1] < '0' || name[1] > '9')
            return false;
        number = (unsigned int)(name[1] - '0');
        if (name_length == 3U) {
            if (name[2] < '0' || name[2] > '9')
                return false;
            number = number * 10U + (unsigned int)(name[2] - '0');
        }
        return number <= 17U;
    }
    if (target->architecture != MINIC_TARGET_ARCH_RISCV64 &&
        target->architecture != MINIC_TARGET_ARCH_RISCV32)
        return false;
    return name_length == 2U && name[0] == 'a' && name[1] >= '0' && name[1] <= '7';
}

bool minic_target_info_inline_asm_register_clobber_supported(const MinicTargetInfo *target,
                                                             const char *name,
                                                             size_t name_length) {
    if (target == NULL || name == NULL) return false;
    /* Syntax-only GNU x86-64 SysV caller-clobber catalog, shared by every
     * x86 Linux asm statement. Naming a physical register does NOT imply
     * that the backend may emit the enclosing template: complete operand,
     * register allocation, clobber and stack-state ABI qualification still
     * happens in the x86 machine owner. In particular RBX, RBP, RSP,
     * R12..R15 and the callee-saved vector registers are NOT admitted.
     * RBX/EBX are never admitted as local fixed-register bindings.
     * They may appear in GNU C source clobber syntax only through the
     * separate, explicitly parse-only predicate below. A real inline ASM
     * target contract must save/restore RBX if it ever uses that clobber.
     * Preserve the old GNU "dx" spelling of physical RDX. */
    if (target->architecture == MINIC_TARGET_ARCH_X86_64) {
        static const char *const caller_scratch[] = {
            "cc", "dx", "rdx", "edx",
            "rax", "eax", "rcx", "ecx",
            "rsi", "esi", "rdi", "edi",
            "r8", "r9", "r10", "r11"
        };
        size_t j;
        for (j = 0U; j < sizeof(caller_scratch) / sizeof(caller_scratch[0]); ++j)
            if (strlen(caller_scratch[j]) == name_length &&
                memcmp(name, caller_scratch[j], name_length) == 0)
                return true;
        return false;
    }
    if (target->architecture == MINIC_TARGET_ARCH_AARCH64)
        return name_length == 2U && memcmp(name, "cc", 2U) == 0;
    /* NZCV flags are only consumed by a fully qualified AArch64 asm shape;
     * recognizing cc never permits an arbitrary machine instruction. */
    if (target->architecture == MINIC_TARGET_ARCH_RISCV32) {
        /* Admission is separate from allocation: an explicit t-register
         * clobber must be respected by the RV32 source-asm register planner.
         * RV32 caller-clobber sets match the base RISC-V integer ABI. */
        return name_length == 2U &&
               ((name[0] == 'a' && name[1] >= '0' && name[1] <= '7') ||
                (name[0] == 't' && name[1] >= '0' && name[1] <= '6'));
    }
    if (target->architecture == MINIC_TARGET_ARCH_LOONGARCH64) {
        /* In original Linux GNU LoongArch Kbuild C, "$tN" and "tN"
         * denote one caller-saved physical scratch register.
         * Admission of names is distinct from machine asm qualification.
         * arch/loongarch/kernel/traps.c additionally declares all eight
         * argument GPRs and callee-saved $s0 in the exact IRQ-stack-switch
         * inline asm. $s0 is syntax-only here: an actual target opcode
         * must both save and restore it, and preserve $ra across its BL.
         * No generic template acquires permission to clobber either. */
        if (name_length == 3U && name[0] == '$') {
            ++name;
            --name_length;
        }
        return name_length == 2U &&
               ((name[0] == 't' && name[1] >= '0' && name[1] <= '8') ||
                (name[0] == 'a' && name[1] >= '0' && name[1] <= '7') ||
                (name[0] == 's' && name[1] == '0'));
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

    /* GNU syntax and local-fixed-register binding are two different contracts.
 * Only parse original Linux x86 EBX/RBX clobber NAMES; the fixed-register
 * binding and physical machine checks must still reject callee-saved RBX.
 * No target ISA knowledge escapes this TargetInfo boundary. */
bool minic_target_info_inline_asm_source_clobber_supported(
    const MinicTargetInfo *target,const char *name,size_t length){
    if(!target||!name)return false;
    if(target->architecture==MINIC_TARGET_ARCH_X86_64 &&
       ((length==3U&&memcmp(name,"ebx",3U)==0)||
        (length==3U&&memcmp(name,"rbx",3U)==0)||
        /* GNU x86 also accepts "flags" as the condition-code clobber.
         * Source parsing alone grants no instruction/emitter permission. */
        (length==5U&&memcmp(name,"flags",5U)==0)))
        return true;
    return minic_target_info_inline_asm_register_clobber_supported(
        target,name,length);
}

/* GCC LoongArch <larchintrin.h> family: independently checked builtin
 * signatures, privilege effects and CSR constant operands. This table is
 * description only, NOT an instruction passthrough or a Core ISA branch.
 * There is exactly one semantic registry shared by parsing and emission. */
typedef enum MinicMachineBuiltinScalar {
    MINIC_MACHINE_BUILTIN_VOID = 0,
    MINIC_MACHINE_BUILTIN_CHAR,
    MINIC_MACHINE_BUILTIN_SHORT,
    MINIC_MACHINE_BUILTIN_INT,
    MINIC_MACHINE_BUILTIN_LONG,
    MINIC_MACHINE_BUILTIN_UCHAR,
    MINIC_MACHINE_BUILTIN_USHORT,
    MINIC_MACHINE_BUILTIN_UINT,
    MINIC_MACHINE_BUILTIN_ULONG
} MinicMachineBuiltinScalar;

typedef struct MinicMachineBuiltinDesc {
    const char *suffix;
    const char *mnemonic;
    MinicMachineBuiltinScalar result;
    size_t argc;
    MinicMachineBuiltinScalar arg[3];
    size_t immediate_index;
} MinicMachineBuiltinDesc;

static MinicType minic_machine_builtin_type(const MinicTargetInfo *target,
                                            MinicMachineBuiltinScalar scalar) {
    /* Machine builtins are injected as synthetic frontend function
     * signatures. A plain `char` ABI argument must carry the SELECTED
     * target's signedness, exactly as a parsed C char declaration does.
     * minic_type_char() alone has the default unsigned spelling and was
     * poisoning every LoongArch CRC builtin declaration: the parsed-AST
     * verifier would reject the entire Linux translation unit before Core. */
    switch (scalar) {
    case MINIC_MACHINE_BUILTIN_CHAR: {
        MinicType type;
        return minic_target_info_plain_char_type(target, &type)
            ? type : minic_type_void();
    }
    case MINIC_MACHINE_BUILTIN_SHORT: return minic_type_short();
    case MINIC_MACHINE_BUILTIN_INT: return minic_type_int();
    case MINIC_MACHINE_BUILTIN_LONG: return minic_type_long();
    case MINIC_MACHINE_BUILTIN_UCHAR: return minic_type_unsigned_char();
    case MINIC_MACHINE_BUILTIN_USHORT: return minic_type_unsigned_short();
    case MINIC_MACHINE_BUILTIN_UINT: return minic_type_unsigned_int();
    case MINIC_MACHINE_BUILTIN_ULONG: return minic_type_unsigned_long();
    default: return minic_type_void();
    }
}

static const MinicMachineBuiltinDesc minic_loong_machine_builtins[] = {
    {"cpucfg", "cpucfg", MINIC_MACHINE_BUILTIN_UINT, 1U, {MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_VOID, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"asrtle_d", "asrtle.d", MINIC_MACHINE_BUILTIN_VOID, 2U, {MINIC_MACHINE_BUILTIN_LONG, MINIC_MACHINE_BUILTIN_LONG, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"asrtgt_d", "asrtgt.d", MINIC_MACHINE_BUILTIN_VOID, 2U, {MINIC_MACHINE_BUILTIN_LONG, MINIC_MACHINE_BUILTIN_LONG, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"crc_w_b_w", "crc.w.b.w", MINIC_MACHINE_BUILTIN_INT, 2U, {MINIC_MACHINE_BUILTIN_CHAR, MINIC_MACHINE_BUILTIN_INT, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"crc_w_h_w", "crc.w.h.w", MINIC_MACHINE_BUILTIN_INT, 2U, {MINIC_MACHINE_BUILTIN_SHORT, MINIC_MACHINE_BUILTIN_INT, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"crc_w_w_w", "crc.w.w.w", MINIC_MACHINE_BUILTIN_INT, 2U, {MINIC_MACHINE_BUILTIN_INT, MINIC_MACHINE_BUILTIN_INT, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"crc_w_d_w", "crc.w.d.w", MINIC_MACHINE_BUILTIN_INT, 2U, {MINIC_MACHINE_BUILTIN_LONG, MINIC_MACHINE_BUILTIN_INT, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"crcc_w_b_w", "crcc.w.b.w", MINIC_MACHINE_BUILTIN_INT, 2U, {MINIC_MACHINE_BUILTIN_CHAR, MINIC_MACHINE_BUILTIN_INT, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"crcc_w_h_w", "crcc.w.h.w", MINIC_MACHINE_BUILTIN_INT, 2U, {MINIC_MACHINE_BUILTIN_SHORT, MINIC_MACHINE_BUILTIN_INT, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"crcc_w_w_w", "crcc.w.w.w", MINIC_MACHINE_BUILTIN_INT, 2U, {MINIC_MACHINE_BUILTIN_INT, MINIC_MACHINE_BUILTIN_INT, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"crcc_w_d_w", "crcc.w.d.w", MINIC_MACHINE_BUILTIN_INT, 2U, {MINIC_MACHINE_BUILTIN_LONG, MINIC_MACHINE_BUILTIN_INT, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"iocsrrd_b", "iocsrrd.b", MINIC_MACHINE_BUILTIN_UCHAR, 1U, {MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_VOID, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"iocsrrd_h", "iocsrrd.h", MINIC_MACHINE_BUILTIN_USHORT, 1U, {MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_VOID, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"iocsrrd_w", "iocsrrd.w", MINIC_MACHINE_BUILTIN_UINT, 1U, {MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_VOID, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"iocsrrd_d", "iocsrrd.d", MINIC_MACHINE_BUILTIN_ULONG, 1U, {MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_VOID, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"iocsrwr_b", "iocsrwr.b", MINIC_MACHINE_BUILTIN_VOID, 2U, {MINIC_MACHINE_BUILTIN_UCHAR, MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"iocsrwr_h", "iocsrwr.h", MINIC_MACHINE_BUILTIN_VOID, 2U, {MINIC_MACHINE_BUILTIN_USHORT, MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"iocsrwr_w", "iocsrwr.w", MINIC_MACHINE_BUILTIN_VOID, 2U, {MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"iocsrwr_d", "iocsrwr.d", MINIC_MACHINE_BUILTIN_VOID, 2U, {MINIC_MACHINE_BUILTIN_ULONG, MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_VOID}, SIZE_MAX},
    {"csrrd_w", "csrrd", MINIC_MACHINE_BUILTIN_UINT, 1U, {MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_VOID, MINIC_MACHINE_BUILTIN_VOID}, 0U},
    {"csrrd_d", "csrrd", MINIC_MACHINE_BUILTIN_ULONG, 1U, {MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_VOID, MINIC_MACHINE_BUILTIN_VOID}, 0U},
    {"csrwr_w", "csrwr", MINIC_MACHINE_BUILTIN_UINT, 2U, {MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_VOID}, 1U},
    {"csrwr_d", "csrwr", MINIC_MACHINE_BUILTIN_ULONG, 2U, {MINIC_MACHINE_BUILTIN_ULONG, MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_VOID}, 1U},
    {"csrxchg_w", "csrxchg", MINIC_MACHINE_BUILTIN_UINT, 3U, {MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_UINT, MINIC_MACHINE_BUILTIN_UINT}, 2U},
    {"csrxchg_d", "csrxchg", MINIC_MACHINE_BUILTIN_ULONG, 3U, {MINIC_MACHINE_BUILTIN_ULONG, MINIC_MACHINE_BUILTIN_ULONG, MINIC_MACHINE_BUILTIN_UINT}, 2U},
};

bool minic_target_info_machine_builtin_signature(
    const MinicTargetInfo *target, const char *spelling, size_t length,
    char *callee, size_t callee_capacity, MinicType *result,
    MinicType arguments[3], size_t *argument_count,
    const char **mnemonic, size_t *immediate_index) {
    static const char gnu_prefix[] = "__builtin_loongarch_";
    static const char internal_prefix[] = "__minic_target_builtin_loongarch_";
    const MinicMachineBuiltinDesc *descriptor;
    size_t i, suffix_length, prefix_length;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        spelling == NULL || result == NULL || arguments == NULL ||
        argument_count == NULL || mnemonic == NULL || immediate_index == NULL ||
        callee == NULL || callee_capacity == 0U ||
        length <= sizeof(gnu_prefix) - 1U ||
        memcmp(spelling, gnu_prefix, sizeof(gnu_prefix) - 1U) != 0)
        return false;
    suffix_length = length - (sizeof(gnu_prefix) - 1U);
    prefix_length = sizeof(internal_prefix) - 1U;
    if (suffix_length > SIZE_MAX - prefix_length ||
        prefix_length + suffix_length >= callee_capacity)
        return false;
    for (i = 0U;
         i < sizeof(minic_loong_machine_builtins) / sizeof(minic_loong_machine_builtins[0]);
         ++i) {
        size_t j;
        descriptor = &minic_loong_machine_builtins[i];
        if (strlen(descriptor->suffix) != suffix_length ||
            memcmp(spelling + sizeof(gnu_prefix) - 1U,
                   descriptor->suffix, suffix_length) != 0)
            continue;
        (void)memcpy(callee, internal_prefix, prefix_length);
        (void)memcpy(callee + prefix_length, descriptor->suffix, suffix_length);
        callee[prefix_length + suffix_length] = '\0';
        *result = minic_machine_builtin_type(target, descriptor->result);
        for (j = 0U; j < descriptor->argc; ++j)
            arguments[j] = minic_machine_builtin_type(target, descriptor->arg[j]);
        *argument_count = descriptor->argc;
        *mnemonic = descriptor->mnemonic;
        *immediate_index = descriptor->immediate_index;
        return true;
    }
    return false;
}

bool minic_target_info_inline_asm_immediate_constraint_supported(const MinicTargetInfo *target,
                                                                 const char *constraint,
                                                                 size_t constraint_length,
                                                                 int64_t value) {
    if (target == NULL || constraint == NULL || constraint_length != 1U ||
        constraint[0] != 'I') {
        return false;
    }
    switch (target->architecture) {
    case MINIC_TARGET_ARCH_RISCV32:
    case MINIC_TARGET_ARCH_RISCV64:
    case MINIC_TARGET_ARCH_LOONGARCH64:
        /* GCC: signed 12-bit immediate. */
        return value >= -2048 && value <= 2047;
    case MINIC_TARGET_ARCH_AARCH64:
        /* AArch64 GCC: add/sub immediate encodes an unsigned imm12,
         * optionally left-shifted 12. This admits Linux's "brk %0"
         * operand 0x401 without borrowing RISC-V's signed range.
         * A positive constraint result is not permission to emit an
         * arbitrary inline-asm template: the target backend qualifies it. */
        return (value >= 0 && value <= 4095) ||
               (value >= 4096 && value <= 0xfff000 &&
                (value & 4095) == 0);
    case MINIC_TARGET_ARCH_X86_64:
        /* GCC config/i386/constraints.md: I is the immediate 0..31
         * (32-bit shifts), not the signed imm12 of RISC-V/LoongArch.
         * Parsing an ISA-specific constant does not authorize generating
         * an unqualified machine ASM template: Core and x86 recheck. */
        return value >= 0 && value <= 31;
    default:
        return false;
    }
}

bool minic_target_info_inline_asm_immediate_may_defer(const MinicTargetInfo *target) {
    /* GNU inline constant operands are often formal-parameter expressions
     * until __always_inline__ specialization. Neither acceptance nor emitted
     * ISA is implied here; Core must resolve the immediate or reject. */
    return target != NULL &&
           (target->architecture == MINIC_TARGET_ARCH_LOONGARCH64 ||
            target->architecture == MINIC_TARGET_ARCH_AARCH64 ||
            target->architecture == MINIC_TARGET_ARCH_X86_64);
}

const char *minic_target_info_inline_asm_immediate_constraint_diagnostic(
    const MinicTargetInfo *target) {
    if (target != NULL && target->architecture == MINIC_TARGET_ARCH_AARCH64) {
        return "GNU asm 'I' input requires an AArch64 unsigned imm12 (optional shift 12)";
    }
    if (target != NULL && target->architecture == MINIC_TARGET_ARCH_X86_64) {
        return "GNU asm 'I' input requires an x86 shift count 0..31";
    }
    return "GNU asm 'I' input requires a signed 12-bit integer constant";
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
