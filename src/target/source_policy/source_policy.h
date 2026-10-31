#ifndef MINIC_TARGET_ASM_SOURCE_POLICY_H
#define MINIC_TARGET_ASM_SOURCE_POLICY_H

#include "target/target_info.h"

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
    bool (*immediate_constraint)(const char *, size_t, int64_t);
    bool immediate_may_defer;
    const char *immediate_diagnostic;
} MinicTargetAsmSourcePolicy;

/* Native machine builtins are target language extensions, not inline-ASM
 * syntax. They have an independent source signature policy and separate
 * native emitter qualification. */
typedef struct MinicTargetMachineBuiltinPolicy {
    bool (*signature)(
        const MinicTargetInfo *, const char *, size_t,
        char *, size_t, MinicType *, MinicType[3], size_t *,
        const char **, size_t *);
} MinicTargetMachineBuiltinPolicy;

/* C-compatible GNU aliases are universally valid and checked by TargetInfo.
 * Only a qualified target may grant an ABI TRANSPORT exception for a pair of
 * incompatible source prototypes. The target owns the complete named ABI
 * family and all argument shapes, not a generic pointer/int width shortcut. */
typedef struct MinicTargetAliasAbiPolicy {
    bool (*allow_incompatible_prototypes)(
        const MinicTargetInfo *, const MinicC0Program *,
        const MinicFunction *, const MinicFunction *);
} MinicTargetAliasAbiPolicy;

/* Target-owned source-language vocabulary: naming a header-only type or
 * function attribute never grants a vector ABI, instruction extension, or
 * linker/ELF contract. A caller checks C syntax/arity; the registered target
 * decides which source spellings and qualified values it may admit. */
typedef struct MinicTargetSourceLanguagePolicy {
    bool (*named_builtin_type)(const MinicTargetInfo *,
                               const char *, size_t, MinicType *);
    bool (*header_opaque_return)(const MinicTargetInfo *, MinicType);
    bool (*header_attribute)(const MinicTargetInfo *, const char *, size_t);
    bool (*gnu_function_attribute_name)(const char *, size_t);
    bool (*gnu_function_attribute_value)(
        MinicTargetGnuFunctionAttributeKind, const char *, size_t);
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
extern const MinicTargetSourceLanguagePolicy minic_x86_64_source_language_policy;
extern const MinicTargetSourceLanguagePolicy minic_aarch64_source_language_policy;
extern const MinicTargetVaCursorSourceAbiPolicy minic_x86_64_va_cursor_source_abi_policy;
extern const MinicTargetVaCursorSourceAbiPolicy minic_aarch64_va_cursor_source_abi_policy;
extern const MinicTargetAsmSourcePolicy minic_loongarch64_asm_source_policy;
extern const MinicTargetMachineBuiltinPolicy minic_loongarch64_machine_builtin_policy;
extern const MinicTargetAliasAbiPolicy minic_loongarch64_alias_abi_policy;

#endif
