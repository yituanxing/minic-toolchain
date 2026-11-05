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

/* Header-only AArch64 nominal types stay target-owned. Four NEON byte/word
 * spellings have genuine 16-byte typed Core storage; other ACLE short-vector
 * names are source declarations only, with no ABI, arithmetic or ISA grant.
 * The IDs are local to this policy, never a generic C enum. */
static const MinicTargetSourceBuiltinTypeDescriptor aarch_builtin_types[] = {
    {1U, "__Float32x4_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {2U, "__Float64x2_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
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
    /* GNU AArch64 arm_neon.h and AAPCS64 short-vector spelling inventory.
     * These are declaration-only nominal identities. In particular, the
     * 64-bit D vectors and the 128-bit kinds without typed MachineIR paths
     * get no storage/ABI/intrinsic/ISA authority from a typedef alone. */
    {10U, "__Int8x8_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {11U, "__Int16x4_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {12U, "__Int32x2_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {13U, "__Int64x1_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {14U, "__Float16x4_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {15U, "__Float32x2_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {16U, "__Float64x1_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {17U, "__Poly8x8_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {18U, "__Poly16x4_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {19U, "__Poly64x1_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {20U, "__Uint8x8_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {21U, "__Uint16x4_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {22U, "__Uint32x2_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {23U, "__Uint64x1_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {24U, "__Int16x8_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {25U, "__Int32x4_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {26U, "__Int64x2_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {27U, "__Float16x8_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {28U, "__Poly16x8_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {29U, "__Poly64x2_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {30U, "__Uint16x8_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {31U, "__Uint32x4_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {32U, "__Bfloat16x4_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE},
    {33U, "__Bfloat16x8_t", MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE,
     MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS,
     MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE, MINIC_TARGET_BUILTIN_SCALAR_NONE}
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
    .header_opaque_return = aarch_header_opaque_return,
    .header_attribute = aarch_header_attribute,
    .gnu_pragma_target_options = aarch_pragma_target_options,
    .gnu_pragma_internal_header = aarch_pragma_internal_header,
};

const MinicTargetVaCursorSourceAbiPolicy minic_aarch64_va_cursor_source_abi_policy = {
    .source_shape = {MINIC_VA_CURSOR_SOURCE_OPAQUE_RECORD, 32U, 8U},
};
