#ifndef MINIC_TARGET_X86_64_MACHINE_INTERNAL_H
#define MINIC_TARGET_X86_64_MACHINE_INTERNAL_H
#include "target/x86_64/machine_emit.h"

/* Native SysV/x87/LP64 scalar frame transport shared by the independently
 * owned machine instruction families. NOT an ISA-independent ABI. */
bool x86_one_gp_load(FILE *, size_t);
bool x86_one_gp_store(FILE *, size_t);
bool x86_normalize(FILE *, const MinicC0Program *, const MinicTargetInfo *, MinicType);
bool x86_value_load(FILE *, const MinicX86Frame *, MinicCoreValueId, const char *);
bool x86_value_store(FILE *, const MinicX86Frame *, MinicCoreValueId);
bool x86_x87_load(FILE *, const MinicX86Frame *, MinicCoreValueId);
bool x86_x87_store(FILE *, const MinicX86Frame *, MinicCoreValueId);
bool x86_i128_load(FILE *, const MinicX86Frame *, MinicCoreValueId,
                   const char *, const char *);
bool x86_i128_store(FILE *, const MinicX86Frame *, MinicCoreValueId,
                    const char *, const char *);
bool x86_emit_indirect_inline_thunk(FILE *, const char *target_register,
                                    bool is_call);
bool x86_object_address(FILE *, const MinicX86Frame *, MinicCoreObjectId, const char *);
bool x86_load_memory(FILE *, size_t, bool);
bool x86_store_memory(FILE *, size_t);

bool minic_x86_64_emit_fp_instruction(
    FILE *, const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicX86Frame *,
    const MinicCoreInstruction *);
bool minic_x86_64_emit_structured_asm(
    FILE *, const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicX86Frame *,
    const MinicCoreInstruction *, size_t);
bool minic_x86_64_emit_linux_smap_alternative(
    FILE *, const MinicCoreFunction *, const MinicX86Frame *,
    const MinicCoreInstruction *, const MinicCoreInlineAsm *);

bool minic_x86_64_emit_call_instruction(
    FILE *, const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicX86Frame *,
    const MinicCoreInstruction *, size_t instruction_id);
#endif
