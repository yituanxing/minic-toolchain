#include "target/source_policy/source_policy.h"
#include "target/machine_register.h"

#include <string.h>

static bool aarch_register_is(const char *name, size_t length, const char *expected) {
    return strlen(expected) == length && memcmp(name, expected, length) == 0;
}

static bool aarch_register_clobber(const char *name, size_t length) {
    /* Recognizing NZCV 'cc' never authorizes arbitrary machine asm. */
    return aarch_register_is(name, length, "cc");
}

static bool aarch_memory_input(const char *constraint, size_t length) {
    /* GCC AArch64 Q: one base-register addressing mode, no displacement. */
    return length == 1U && constraint[0] == 'Q';
}

static bool aarch_immediate(const char *constraint, size_t length, int64_t value) {
    /* GCC AArch64 'I': unsigned imm12, optionally left shifted by 12. */
    return length == 1U && constraint[0] == 'I' &&
           ((value >= 0 && value <= 4095) ||
            (value >= 4096 && value <= 0xfff000 && (value & 4095) == 0));
}

const MinicTargetAsmSourcePolicy minic_aarch64_asm_source_policy = {
    .fixed_register_read = minic_aarch64_machine_fixed_register_read,
    .local_fixed_register = minic_aarch64_machine_local_fixed_register,
    .register_clobber = aarch_register_clobber,
    .memory_input_constraint = aarch_memory_input,
    .immediate_constraint = aarch_immediate,
    .immediate_may_defer = true,
    .immediate_diagnostic =
        "GNU asm 'I' input requires an AArch64 unsigned imm12 (optional shift 12)",
};

/* The original five names remain opaque header-only identities. The four
 * explicitly registered NEON byte/word spellings additionally have real
 * target-owned 16-byte object layout, but still have NO vector arithmetic,
 * V-register transport or function-call PCS implementation. */
static bool aarch_named_header_builtin(const MinicTargetInvocation *target,
                                       const char *name, size_t length,
                                       MinicType *type) {
    static const struct {
        const char *name;
        MinicTypeBaseKind kind;
    } names[] = {
        {"__Float32x4_t", MINIC_TYPE_BASE_AARCH64_FLOAT32X4},
        {"__Float64x2_t", MINIC_TYPE_BASE_AARCH64_FLOAT64X2},
        {"__SVFloat32_t", MINIC_TYPE_BASE_AARCH64_SVE_FLOAT32},
        {"__SVFloat64_t", MINIC_TYPE_BASE_AARCH64_SVE_FLOAT64},
        {"__SVBool_t", MINIC_TYPE_BASE_AARCH64_SVE_BOOL},
        {"__Uint8x16_t", MINIC_TYPE_BASE_AARCH64_UINT8X16},
        {"__Uint64x2_t", MINIC_TYPE_BASE_AARCH64_UINT64X2},
        {"__Int8x16_t", MINIC_TYPE_BASE_AARCH64_INT8X16},
        {"__Poly8x16_t", MINIC_TYPE_BASE_AARCH64_POLY8X16}
    };
    size_t i;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (aarch_register_is(name, length, names[i].name) &&
            minic_target_info_language_type_supported(target, names[i].kind)) {
            if (type != NULL)
                *type = minic_type_header_builtin(names[i].kind);
            return true;
        }
    }
    return false;
}

static bool aarch_header_opaque_return(const MinicTargetInvocation *target,
                                       MinicType type) {
    if (type.pointer_depth != 0U ||
        !minic_target_info_language_type_supported(target, type.base_kind))
        return false;
    switch (type.base_kind) {
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

static bool aarch_header_attribute(const MinicTargetInvocation *target,
                                   const char *name, size_t length) {
    return target != NULL &&
           (target->source_info.language_type_capabilities &
            MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS) != 0U &&
           aarch_register_is(name, length, "__aarch64_vector_pcs__");
}

/* GCC's arm_neon.h selects the architectural AdvSIMD baseline with
 * `target("+nothing+simd")`. AArch64's existing scalar FP emitter already
 * operates under this ISA baseline. This is ONLY source option selection:
 * it grants no NEON vector type, intrinsic, V-register ASM or ABI lowering.
 * In particular, all other GCC target extension strings remain rejected. */
static bool aarch_pragma_target_options(
    const MinicTargetInvocation *target, const char *value, size_t value_length,
    const MinicTargetGnuPragmaOptionState *current,
    MinicTargetGnuPragmaOptionState *next) {
    static const char simd_baseline[] = "+nothing+simd";
    (void)current;
    if (target == NULL || value == NULL || next == NULL ||
        !aarch_register_is(value, value_length, simd_baseline))
        return false;
    /* This mask is interpreted only by the AArch64 policy. The parser
     * copies opaque snapshots; no machine emitter consumes this bit yet. */
    next->selected_source_features = 1U;
    return true;
}

const MinicTargetSourceLanguagePolicy minic_aarch64_source_language_policy = {
    .named_builtin_type = aarch_named_header_builtin,
    .header_opaque_return = aarch_header_opaque_return,
    .header_attribute = aarch_header_attribute,
    .gnu_pragma_target_options = aarch_pragma_target_options,
};

const MinicTargetVaCursorSourceAbiPolicy minic_aarch64_va_cursor_source_abi_policy = {
    .source_shape = {MINIC_VA_CURSOR_SOURCE_OPAQUE_RECORD, 32U, 8U},
};
