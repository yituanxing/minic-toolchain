#include "target/source_policy/source_policy.h"
#include "target/machine_register.h"

#include <string.h>

static bool aarch_register_is(const char *name, size_t length, const char *expected) {
    return strlen(expected) == length && memcmp(name, expected, length) == 0;
}

/* GCC13 aarch64-builtins.cc registers these exact spellings against
 * intQI_type_node and intDI_type_node. LP64's intDI is canonical long
 * (GCC tree.cc chooses LONG_TYPE_SIZE before LONG_LONG_TYPE_SIZE).
 * In the header they appear only as
 * typed pointer casts for ld1/st1; they are ordinary signed C scalars,
 * not fresh vectors or a permission to emit other AdvSIMD instructions. */
static bool aarch_named_scalar_type(const char *name, size_t length, MinicType *type) {
    if (aarch_register_is(name, length, "__builtin_aarch64_simd_qi")) {
        if (type != NULL) *type = minic_type_signed_char();
        return true;
    }
    if (aarch_register_is(name, length, "__builtin_aarch64_simd_di")) {
        if (type != NULL) *type = minic_type_long();
        return true;
    }
    return false;
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

/* Header-only AArch64 nominal types stay target-owned. Integer/polynomial
 * and binary32/binary64 D/Q register lanes have genuine typed Core storage
 * and AAPCS64 ABI. Integer ADD/XOR and floating ADD select their verified
 * element width independently; float16/SVE/other ACLE names remain source
 * declarations only, with no arithmetic or ISA grant.
 * The IDs are local to this policy, never a generic C enum. */
static const MinicTargetSourceBuiltinTypeDescriptor aarch_builtin_types[] = {
    {1U, "__Float32x4_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128, MINIC_TARGET_BUILTIN_SCALAR_FLOAT32},
    {2U, "__Float64x2_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128, MINIC_TARGET_BUILTIN_SCALAR_FLOAT64},
    {3U, "__SVFloat32_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_OPAQUE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {4U, "__SVFloat64_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_OPAQUE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {5U, "__SVBool_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_OPAQUE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {6U, "__Uint8x16_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128,
     MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_CHAR},
    {7U, "__Uint64x2_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128,
     MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_LONG_LONG},
    {8U, "__Int8x16_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128,
     MINIC_TARGET_BUILTIN_SCALAR_SIGNED_CHAR},
    {9U, "__Poly8x16_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128,
     MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_CHAR},
    /* Integer D-register vectors have genuine 64-bit Core storage. Each
     * element width remains part of the Core type identity and selects the
     * architectural lane arrangement; AAPCS64 independently checks ABI. */
    {10U, "__Int8x8_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR64_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR64,
     MINIC_TARGET_BUILTIN_SCALAR_SIGNED_CHAR},
    {11U, "__Int16x4_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR64_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR64, MINIC_TARGET_BUILTIN_SCALAR_SIGNED_SHORT},
    {12U, "__Int32x2_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR64_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR64, MINIC_TARGET_BUILTIN_SCALAR_SIGNED_INT},
    {13U, "__Int64x1_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR64_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR64, MINIC_TARGET_BUILTIN_SCALAR_SIGNED_LONG_LONG},
    {14U, "__Float16x4_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {15U, "__Float32x2_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR64_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR64, MINIC_TARGET_BUILTIN_SCALAR_FLOAT32},
    {16U, "__Float64x1_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR64_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR64, MINIC_TARGET_BUILTIN_SCALAR_FLOAT64},
    {17U, "__Poly8x8_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR64_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR64, MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_CHAR},
    {18U, "__Poly16x4_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR64_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR64, MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_SHORT},
    {19U, "__Poly64x1_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR64_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR64, MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_LONG_LONG},
    {20U, "__Uint8x8_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR64_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR64,
     MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_CHAR},
    {21U, "__Uint16x4_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR64_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR64, MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_SHORT},
    {22U, "__Uint32x2_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR64_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR64, MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_INT},
    {23U, "__Uint64x1_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR64_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR64, MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_LONG_LONG},
    {24U, "__Int16x8_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128, MINIC_TARGET_BUILTIN_SCALAR_SIGNED_SHORT},
    {25U, "__Int32x4_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128, MINIC_TARGET_BUILTIN_SCALAR_SIGNED_INT},
    {26U, "__Int64x2_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128, MINIC_TARGET_BUILTIN_SCALAR_SIGNED_LONG_LONG},
    {27U, "__Float16x8_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {28U, "__Poly16x8_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128, MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_SHORT},
    {29U, "__Poly64x2_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128, MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_LONG_LONG},
    {30U, "__Uint16x8_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128, MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_SHORT},
    {31U, "__Uint32x4_t", MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE,
     MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128, MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_INT},
    {32U, "__Bfloat16x4_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {33U, "__Bfloat16x8_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    /* GCC's real arm_neon.h immediately follows its short-vector typedefs
     * with four distinct polynomial scalar typedefs, FP8 vector identities
     * and __fp16. Until their exact C storage, conversions and AAPCS64 PCS
     * have target-owned models, admit only header typedef declarations. */
    {34U, "__Poly8_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {35U, "__Poly16_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {36U, "__Poly64_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {37U, "__Poly128_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {38U, "__Mfloat8x8_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {39U, "__Mfloat8x16_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {40U, "__fp16", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE}
};

/* Only four GCC13 arm_neon.h AdvSIMD memory builtins are executable here.
 * The compiler preserves the original spelling in the parsed wrapper body;
 * this target source policy alone maps it to an unspellable Core callee. */
static bool aarch_machine_builtin_signature(
    const MinicTargetInvocation *target, const char *spelling, size_t length,
    char *callee, size_t capacity, MinicType *result,
    MinicType arguments[3], size_t *argc,
    const char **mnemonic, size_t *immediate_index) {
    static const char source_prefix[] = "__builtin_aarch64_";
    static const char internal_prefix[] = "__minic_target_builtin_aarch64_";
    const char *suffix;
    size_t suffix_length;
    bool load, bytes;
    bool header_only_reduction;
    MinicType scalar, pointer, vector;
    if (target == NULL || spelling == NULL || callee == NULL || result == NULL ||
        arguments == NULL || argc == NULL || mnemonic == NULL ||
        immediate_index == NULL || length <= sizeof(source_prefix) - 1U ||
        memcmp(spelling, source_prefix, sizeof(source_prefix) - 1U) != 0)
        return false;
    suffix = spelling + sizeof(source_prefix) - 1U;
    suffix_length = length - (sizeof(source_prefix) - 1U);
    /* GCC13 declares this signed widening add-long (8 x i8 -> 8 x i16)
     * in the same system header. Source signature only: native emission
     * is NOT granted, including when directly called from user code. */
    header_only_reduction = aarch_register_is(suffix, suffix_length, "saddlv8qi");
    load = aarch_register_is(suffix, suffix_length, "ld1v16qi_us") ||
           aarch_register_is(suffix, suffix_length, "ld1v2di_us");
    if (!header_only_reduction && !load &&
        !aarch_register_is(suffix, suffix_length, "st1v16qi_su") &&
        !aarch_register_is(suffix, suffix_length, "st1v2di_su")) return false;
    bytes = aarch_register_is(suffix, suffix_length, "ld1v16qi_us") ||
            aarch_register_is(suffix, suffix_length, "st1v16qi_su");
    if (sizeof(internal_prefix) - 1U + suffix_length >= capacity) return false;
    (void)memcpy(callee, internal_prefix, sizeof(internal_prefix) - 1U);
    (void)memcpy(callee + sizeof(internal_prefix) - 1U, suffix, suffix_length);
    callee[sizeof(internal_prefix) - 1U + suffix_length] = '\0';
    if (header_only_reduction) {
        *result = minic_type_header_builtin(
            24U, MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE);
        arguments[0] = minic_type_header_builtin(
            10U, MINIC_BUILTIN_DECLARATION_FIXED_VECTOR64_STORAGE);
        if (!minic_target_info_language_type_supported_for_type(target, *result) ||
            !minic_target_info_language_type_supported_for_type(target, arguments[0]))
            return false;
        *argc = 1U;
        *mnemonic = "unsupported-aarch64-saddlv8qi";
        *immediate_index = SIZE_MAX;
        return true;
    }
    /* _us/_su use signed intQI/intDI pointer ingress, even though their
     * actual Q-register result/input remains an unsigned vector. */
    scalar = bytes ? minic_type_signed_char() : minic_type_long();
    if (load && !minic_type_add_const(scalar, &scalar)) return false;
    if (!minic_type_pointer_to(scalar, &pointer)) return false;
    vector = minic_type_header_builtin(bytes ? 6U : 7U,
                                      MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE);
    if (!minic_target_info_language_type_supported_for_type(target, vector)) return false;
    arguments[0] = pointer;
    if (load) {
        *result = vector;
        *argc = 1U;
    } else {
        *result = minic_type_void();
        arguments[1] = vector;
        *argc = 2U;
    }
    *mnemonic = load ? "ldr" : "str";
    *immediate_index = SIZE_MAX;
    return true;
}

const MinicTargetMachineBuiltinPolicy minic_aarch64_machine_builtin_policy = {
    .signature = aarch_machine_builtin_signature,
};

static bool aarch_header_opaque_return(const MinicTargetInvocation *target,
                                       MinicType type) {
    MinicTargetSourceBuiltinTypeDescriptor descriptor;
    return type.pointer_depth == 0U &&
           minic_target_info_source_builtin_type_descriptor(
               target, type, &descriptor) &&
           descriptor.role == MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE;
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

static bool aarch_pragma_internal_header(
    const MinicTargetInvocation *target, const char *directive, size_t directive_length,
    const char *header, size_t header_length,
    const MinicTargetGnuPragmaOptionState *current) {
    /* GCC's real AArch64 arm_neon.h emits this marker after its
     * push_options/target("+nothing+simd") prefix. It marks GCC's own
     * system-header intrinsic declarations; in MiniC the target source
     * registry remains the only authority for supported vector vocabulary.
     * No arbitrary GCC extension pragma or unselected ISA is accepted. */
    return target != NULL && current != NULL &&
           current->selected_source_features == 1U &&
           (target->source_info.language_type_capabilities &
            MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS) != 0U &&
           aarch_register_is(directive, directive_length, "aarch64") &&
           aarch_register_is(header, header_length, "arm_neon.h");
}

const MinicTargetSourceLanguagePolicy minic_aarch64_source_language_policy = {
    .builtin_types = aarch_builtin_types,
    .builtin_type_count = sizeof(aarch_builtin_types) / sizeof(aarch_builtin_types[0]),
    .named_scalar_type = aarch_named_scalar_type,
    .header_opaque_return = aarch_header_opaque_return,
    .header_attribute = aarch_header_attribute,
    .gnu_pragma_target_options = aarch_pragma_target_options,
    .gnu_pragma_internal_header = aarch_pragma_internal_header,
};

const MinicTargetVaCursorSourceAbiPolicy minic_aarch64_va_cursor_source_abi_policy = {
    .source_shape = {MINIC_VA_CURSOR_SOURCE_OPAQUE_RECORD, 32U, 8U},
};
