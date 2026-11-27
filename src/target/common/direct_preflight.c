#include "target/common/direct_preflight.h"

#include "target/common/elf_alias_emit.h"
#include "target/common/lp64_scalar.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/lp64_frame.h"
#include <stdint.h>
#include <stdlib.h>

/* Only typed structured asm and target-whitelisted, zero-operand opaque
 * hardware instructions are admitted. Source-to-Core one-to-one ownership
 * stays mandatory. Neither target may silently drop a hardware operation. */
static bool direct_lp64_only_structured_asm(
    const MinicCoreFunction *fn,
    MinicDirectLP64OpaqueAsmQualifier qualify_opaque_asm) {
    bool *used;
    size_t i, count = 0U;
    bool accepted = false;
    if (!fn || fn->type_context == NULL) return false;
    if (fn->inline_asm_count == 0U) return true;
    if (fn->inline_asms == NULL ||
        (fn->instruction_count != 0U && fn->instructions == NULL) ||
        fn->inline_asm_count > SIZE_MAX / sizeof(*used))
        return false;
    used = (bool *)calloc(fn->inline_asm_count, sizeof(*used));
    if (used == NULL) return false;
    for (i = 0U; i < fn->instruction_count; ++i) {
        const MinicCoreInstruction *ins = &fn->instructions[i];
        if (ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) {
            const size_t id = ins->value.structured_inline_asm.inline_asm_id;
            const size_t operand_count = ins->value.structured_inline_asm.operand_count;
            if (id >= fn->inline_asm_count || used[id] ||
                fn->inline_asms[id].is_goto ||
                fn->inline_asms[id].template_text == NULL ||
                ins->result != MINIC_CORE_VALUE_INVALID ||
                !minic_lp64_core_kind(fn->type_context, ins->core_type_id,
                                      MINIC_CORE_TYPE_VOID))
                goto done;
            /* The generic GNU fallback previously published these as two
             * RISC-V-only scalar instruction kinds. Unifying Core's operand
             * representation must not silently grant x86/A64/Loong a new
             * hardware ASM capability. Already-qualified target contracts
             * and the existing two-input / +r structured forms are intact. */
            if (fn->inline_asms[id].operand_plan_owner == MINIC_ASM_OPERAND_PLAN_NONE &&
                ((operand_count == 1U &&
                  ins->value.structured_inline_asm.operands[0].kind ==
                      MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
                 (operand_count == 2U &&
                  ins->value.structured_inline_asm.operands[0].kind ==
                      MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE &&
                  ins->value.structured_inline_asm.operands[1].kind ==
                      MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)))
                goto done;
            used[id] = true;
            ++count;
        } else if (ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM) {
            const size_t id = ins->value.inline_asm_id;
            if (qualify_opaque_asm == NULL ||
                id >= fn->inline_asm_count || used[id] ||
                ins->result != MINIC_CORE_VALUE_INVALID ||
                !minic_lp64_core_kind(fn->type_context, ins->core_type_id,
                                      MINIC_CORE_TYPE_VOID) ||
                !qualify_opaque_asm(&fn->inline_asms[id]))
                goto done;
            used[id] = true;
            ++count;
        }
    }
    accepted = count == fn->inline_asm_count;

done:
    free(used);
    return accepted;
}
/* Both new direct LP64 ISA emitters had byte-equivalent admission policy.
 * Reject unsupported Core shapes before generating partial GNU assembly.
 * Caller owns target ABI, registers, frame and any failure diagnostics. */
bool minic_multi_direct_lp64_function_preflight(
    const MinicCoreModuleFunctionSymbol *module_decl,
    const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    MinicDirectLP64OpaqueAsmQualifier qualify_opaque_asm,
    const MinicDirectLP64AbiPolicy *abi) {
    bool verified;
    bool return_valid;
    bool visibility_valid, alias_present;
    if (module_decl == NULL || fn == NULL ||
        fn->type_context == NULL ||
        !minic_direct_lp64_abi_policy_valid(abi)) return false;
    verified = minic_core_function_facts_verified(facts, fn);
    return_valid = minic_lp64_core_valid_return_with_policy(
        fn->type_context, minic_core_function_return_type(fn), abi);
    visibility_valid =
        module_decl->visibility == MINIC_CORE_MODULE_VISIBILITY_DEFAULT ||
        minic_multi_elf_module_visibility_directive(module_decl->visibility) != NULL;
    alias_present = module_decl->alias_target_index != SIZE_MAX;
    if (module_decl->name == NULL || module_decl->name[0] == '\0' ||
        !verified ||
        !visibility_valid ||
        alias_present ||
        !return_valid ||
        fn->block_count > 2048U ||
        !direct_lp64_only_structured_asm(fn, qualify_opaque_asm) ||
        /* The shared ABI plan and both target emitters iterate all argument
         * locations, including the spill region after eight GP registers.
         * Keep function and call-side capacity at the same proven 64. */
        minic_core_function_parameter_count(fn) > MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS ||
        !minic_lp64_baseline_values_fit_frame(fn->value_count) ||
        fn->object_count > 8192U)
        return false;
    return true;
}

bool minic_multi_direct_lp64_cfg_preflight(
    const MinicCoreFunction *fn, size_t *failed_block,
    const MinicDirectLP64AbiPolicy *abi) {
    size_t i;
    if (fn == NULL ||
        fn->type_context == NULL ||
        failed_block == NULL || !minic_direct_lp64_abi_policy_valid(abi)) return false;
    *failed_block = SIZE_MAX;
    /* M2: verify every CFG successor and return before emitting anything.
     * A function's SSA values and local objects retain unique stack slots
     * across blocks, so no architecture-independent register allocator is
     * needed to implement basic control-flow correctly. */
    for (i = 0U; i < fn->block_count; ++i) {
        *failed_block = i;
        const MinicCoreTerminator *term = &fn->blocks[i].terminator;
        /* Canonical Core verifier has already checked terminator presence,
         * CFG edge bounds and SSA return/condition identity. Retain only
         * machine-supported return modes and condition widths. */
        switch (term->kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if (minic_lp64_core_kind(fn->type_context,
                    minic_core_function_return_type(fn), MINIC_CORE_TYPE_RECORD)) {
                size_t bytes, slots, fp_elements, fp_width;
                const MinicCoreTypeContext *types = fn->type_context;
                if ((!minic_multi_empty_record_core(types, minic_core_function_return_type(fn)) &&
                     !minic_multi_gp_record_core(types, minic_core_function_return_type(fn),
                                                 &bytes, &slots) &&
                     !minic_multi_fp_return_record_core(types, minic_core_function_return_type(fn),
                                                        &fp_elements, &fp_width) &&
                     !minic_lp64_core_indirect_result(
                         types, minic_core_function_return_type(fn),
                         abi->core_indirect_return)) ||
                    term->return_value != MINIC_CORE_VALUE_INVALID ||
                    term->return_object >= fn->object_count ||
                    fn->objects == NULL ||
                    minic_core_type_unqualified(types,
                            fn->objects[term->return_object].core_type_id) !=
                        minic_core_type_unqualified(types, minic_core_function_return_type(fn)))
                    return false;
            } else if (term->return_object != MINIC_CORE_OBJECT_INVALID) {
                return false;
            }
            break;
        case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
            break;
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
            /* Verified Core unreachable terminator is a semantic no-return
             * endpoint, not an implicit branch into the next basic block. */
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH: {
            size_t condition_width = 0U;
            bool condition_signed = false;
            /* A Core condition can be an original C narrow integer:
             * cJSON's input-byte loop tests unsigned char directly.
             * The branch emission zero-tests the low 32-bit SSA slot. */
            if (term->conditional.condition >= fn->value_count ||
                fn->values == NULL ||
                !minic_lp64_core_integer_width(fn->type_context,
                    fn->values[term->conditional.condition].core_type_id,
                    &condition_width, &condition_signed))
                return false;
            break;
        }
        default:
            return false;
        }
    }
    return true;
}
