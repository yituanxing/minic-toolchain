#include "target/source_policy/source_policy.h"

#include <string.h>

static bool aarch_register_is(const char *name, size_t length, const char *expected) {
    return strlen(expected) == length && memcmp(name, expected, length) == 0;
}

static bool aarch_fixed_register_read(const char *name, size_t length) {
    return aarch_register_is(name, length, "sp");
}

static bool aarch_local_fixed_register(const char *name, size_t length) {
    unsigned int number;
    /* Linux current_stack_pointer and PAC XPACLRI's tied x30 link-register
     * operand have separately qualified native machine paths. */
    if (aarch_register_is(name, length, "sp") ||
        aarch_register_is(name, length, "x30")) return true;
    /* Source-only AAPCS64 argument and scratch bindings x/r0..17. x18 and
     * all callee-saved registers remain fail-closed. */
    if (length < 2U || length > 3U || (name[0] != 'x' && name[0] != 'r') ||
        name[1] < '0' || name[1] > '9') return false;
    number = (unsigned int)(name[1] - '0');
    if (length == 3U) {
        if (name[2] < '0' || name[2] > '9') return false;
        number = number * 10U + (unsigned int)(name[2] - '0');
    }
    return number <= 17U;
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
    .fixed_register_read = aarch_fixed_register_read,
    .local_fixed_register = aarch_local_fixed_register,
    .register_clobber = aarch_register_clobber,
    .memory_input_constraint = aarch_memory_input,
    .immediate_constraint = aarch_immediate,
    .immediate_may_defer = true,
    .immediate_diagnostic =
        "GNU asm 'I' input requires an AArch64 unsigned imm12 (optional shift 12)",
};

/* These five names are opaque C header identities, not a vector storage,
 * arithmetic, or function-call PCS implementation. A source declaration
 * may mention them only while the selected target explicitly advertises
 * header-only opaque vector declarations. */
static bool aarch_named_header_builtin(const MinicTargetInfo *target,
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
        {"__SVBool_t", MINIC_TYPE_BASE_AARCH64_SVE_BOOL}
    };
    size_t i;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (aarch_register_is(name, length, names[i].name) &&
            minic_target_info_language_type_supported(target, names[i].kind)) {
            if (type != NULL)
                *type = minic_type_aarch64_header_builtin(names[i].kind);
            return true;
        }
    }
    return false;
}

static bool aarch_header_opaque_return(const MinicTargetInfo *target,
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

static bool aarch_header_attribute(const MinicTargetInfo *target,
                                   const char *name, size_t length) {
    return (target->language_type_capabilities &
            MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS) != 0U &&
           aarch_register_is(name, length, "__aarch64_vector_pcs__");
}

const MinicTargetSourceLanguagePolicy minic_aarch64_source_language_policy = {
    .named_builtin_type = aarch_named_header_builtin,
    .header_opaque_return = aarch_header_opaque_return,
    .header_attribute = aarch_header_attribute,
};

const MinicTargetVaCursorSourceAbiPolicy minic_aarch64_va_cursor_source_abi_policy = {
    .source_shape = {MINIC_VA_CURSOR_SOURCE_OPAQUE_RECORD, 32U, 8U},
};
