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

extern const MinicTargetAsmSourcePolicy minic_riscv32_asm_source_policy;
extern const MinicTargetAsmSourcePolicy minic_riscv64_asm_source_policy;
extern const MinicTargetAsmSourcePolicy minic_x86_64_asm_source_policy;
extern const MinicTargetAsmSourcePolicy minic_aarch64_asm_source_policy;
extern const MinicTargetAsmSourcePolicy minic_loongarch64_asm_source_policy;
extern const MinicTargetMachineBuiltinPolicy minic_loongarch64_machine_builtin_policy;

#endif
