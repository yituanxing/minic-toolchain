#ifndef MINIC_TARGET_ASM_SOURCE_POLICY_H
#define MINIC_TARGET_ASM_SOURCE_POLICY_H

#include "target/source_policy/source_info.h"

/* Shared C rank construction for canonical target registration and C source
 * integer promotion; machine emitters never synthesize C scalar types. */
bool minic_target_source_integer_type_with_rank(MinicIntegerSign sign,
                                                MinicIntegerRank rank,
                                                MinicType *result);

/* These callbacks answer C/GNU source grammar questions only. An accepted
 * spelling never reserves a register or authorizes a physical ASM template.
 * Each target owns its own policy object; missing callbacks fail closed. */
typedef struct MinicTargetAsmSourcePolicy {
    bool (*fixed_register_read)(const char *, size_t);
    bool (*fixed_register_write)(const char *, size_t);
    bool (*local_fixed_register)(const char *, size_t);
    bool (*readwrite_hardreg)(const char *, size_t,
                              const char *, size_t, MinicType);
    bool (*register_clobber)(const char *, size_t);
    bool (*source_clobber)(const char *, size_t);
    bool (*memory_input_constraint)(const char *, size_t);
    /* Target-owned GNU SIMD register grammar, without admitting an opcode. */
    bool (*vector_register_input_constraint)(const char *, size_t, MinicType);
    bool (*immediate_constraint)(const char *, size_t, int64_t);
    bool immediate_may_defer;
    const char *immediate_diagnostic;
} MinicTargetAsmSourcePolicy;

/* Native machine builtins are target language extensions, not inline-ASM
 * syntax. They have an independent source signature policy and separate
 * native emitter qualification. */
typedef struct MinicTargetMachineBuiltinPolicy {
    bool (*signature)(
        const MinicTargetInvocation *, const char *, size_t,
        char *, size_t, MinicType *, MinicType[3], size_t *,
        const char **, size_t *);
} MinicTargetMachineBuiltinPolicy;

/* C-compatible GNU aliases are universally valid and checked by TargetInfo.
 * Only a qualified target may grant an ABI TRANSPORT exception for a pair of
 * incompatible source prototypes. The target owns the complete named ABI
 * family and all argument shapes, not a generic pointer/int width shortcut. */
typedef struct MinicTargetAliasAbiPolicy {
    bool (*allow_incompatible_prototypes)(
        const MinicTargetInvocation *, const MinicC0Program *,
        const MinicFunction *, const MinicFunction *);
} MinicTargetAliasAbiPolicy;

/* Target-owned source-language vocabulary: naming a header-only type or
 * function attribute never grants a vector ABI, instruction extension, or
 * linker/ELF contract. A caller checks C syntax/arity; the registered target
 * decides which source spellings and qualified values it may admit. */
typedef struct MinicTargetSourceLanguagePolicy {
    /* Admit a source-only, exact GNU system-header inline liveness prepass.
     * This never grants a builtin signature, ABI or machine instruction. */
    bool gnu_neon_header_inline_liveness;
    /* Immutable target-owned source builtin type registry. One descriptor
     * owns the name, nominal identity, admission and Core ingress shape. */
    const MinicTargetSourceBuiltinTypeDescriptor *builtin_types;
    size_t builtin_type_count;
    MinicTargetNamedBuiltinScalarTypePolicy named_scalar_type;
    bool (*header_opaque_return)(const MinicTargetInvocation *, MinicType);
    bool (*header_attribute)(const MinicTargetInvocation *, const char *, size_t);
    bool (*gnu_function_attribute_name)(const char *, size_t);
    bool (*gnu_function_attribute_value)(
        MinicTargetGnuFunctionAttributeKind, const char *, size_t);
    /* Target-specific GCC #pragma target option policy. No frontend ISA
     * spellings; unknown strings fail closed, without mutating *next. */
    bool (*gnu_pragma_target_options)(
        const MinicTargetInvocation *, const char *, size_t,
        const MinicTargetGnuPragmaOptionState *,
        MinicTargetGnuPragmaOptionState *);
    /* GCC system headers may contain target-private header markers. The
     * source policy admits only exact named markers under the active source
     * option snapshot; no marker enables machine emission on its own. */
    bool (*gnu_pragma_internal_header)(
        const MinicTargetInvocation *, const char *, size_t,
        const char *, size_t, const MinicTargetGnuPragmaOptionState *);
} MinicTargetSourceLanguagePolicy;

/* The frontend builds one canonical genuine C source type per Program from
 * this target-owned byte/alignment/category descriptor. The machine's SSA
 * pointers to mutable ABI state remain separate and are not C type aliases. */
typedef struct MinicTargetVaCursorSourceAbiPolicy {
    /* Data-only canonical C source shape. ABI state-copy and physical cursor
     * storage remain separate; the frontend builds a normal C record/array. */
    MinicVaCursorSourceShape source_shape;
} MinicTargetVaCursorSourceAbiPolicy;

extern const MinicTargetAsmSourcePolicy minic_riscv32_asm_source_policy;
extern const MinicTargetAsmSourcePolicy minic_riscv64_asm_source_policy;
extern const MinicTargetAsmSourcePolicy minic_x86_64_asm_source_policy;
extern const MinicTargetAsmSourcePolicy minic_aarch64_asm_source_policy;
extern const MinicTargetMachineBuiltinPolicy minic_aarch64_machine_builtin_policy;
extern const MinicTargetSourceLanguagePolicy minic_x86_64_source_language_policy;
extern const MinicTargetSourceLanguagePolicy minic_aarch64_source_language_policy;
extern const MinicTargetVaCursorSourceAbiPolicy minic_x86_64_va_cursor_source_abi_policy;
extern const MinicTargetVaCursorSourceAbiPolicy minic_aarch64_va_cursor_source_abi_policy;
extern const MinicTargetAsmSourcePolicy minic_loongarch64_asm_source_policy;
extern const MinicTargetMachineBuiltinPolicy minic_loongarch64_machine_builtin_policy;
extern const MinicTargetAliasAbiPolicy minic_loongarch64_alias_abi_policy;

#endif
