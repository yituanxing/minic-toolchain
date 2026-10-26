#include "target/common/direct_preflight.h"

#include "target/common/global_emit.h"
#include "target/common/lp64_scalar.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Both new direct LP64 ISA emitters had byte-equivalent admission policy.
 * Reject unsupported Core shapes before generating partial GNU assembly.
 * Caller owns target ABI, registers, frame and any failure diagnostics. */
bool minic_multi_direct_lp64_function_preflight(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicFunction *decl, const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts, const char *arch, const char *name) {
    bool verified;
    if (program == NULL || target == NULL || decl == NULL || fn == NULL ||
        arch == NULL) return false;
    verified = minic_core_function_facts_verified(facts, fn) ||
               minic_core_function_verify(fn);
    if (name == NULL || name[0] == '\0' ||
        !verified ||
        decl->is_weak ||
        (decl->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
         minic_multi_elf_visibility_directive(decl->visibility) == NULL) ||
        decl->section_name != NULL || decl->alias_target != MINIC_FUNCTION_INVALID ||
        !minic_lp64_valid_return_type(program, target, fn->return_type) ||
        fn->block_count > 2048U ||
        fn->inline_asm_count != 0U || fn->parameter_count > 16U ||
        fn->value_count > 32768U || fn->object_count > 8192U)
        {
            if (getenv("MINIC_BACKEND_CENSUS") != NULL)
                (void)fprintf(stderr,
                              "MINIC_BACKEND_PREFLIGHT arch=%s function=%s "
                              "blocks=%zu values=%zu objects=%zu params=%zu "
                              "entry=%u inline=%zu return_valid=%d verified=%d "
                              "constructor=%d weak=%d visibility=%d section=%d\n",
                              arch, name != NULL ? name : "<unnamed>",
                              fn->block_count, fn->value_count, fn->object_count,
                              fn->parameter_count, (unsigned int)fn->entry_block,
                              fn->inline_asm_count,
                              minic_lp64_valid_return_type(program, target, fn->return_type) ? 1 : 0,
                              verified ? 1 : 0,
                              decl->is_constructor ? 1 : 0, decl->is_weak ? 1 : 0,
                              (int)decl->visibility,
                              decl->section_name != NULL ? 1 : 0);
            return false;
        }
    return true;
}

bool minic_multi_direct_lp64_cfg_preflight(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, size_t *failed_block) {
    size_t i;
    if (program == NULL || target == NULL || fn == NULL ||
        failed_block == NULL) return false;
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
            /* Current direct LP64 emitter returns scalar values only.
             * The canonical Core return verifier also accepts records. */
            if (term->return_object != MINIC_CORE_OBJECT_INVALID)
                return false;
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
            if (!minic_lp64_valid_int_or_bool(fn->values[term->conditional.condition].type) &&
                !minic_lp64_register64_type(program, target,
                    fn->values[term->conditional.condition].type) &&
                !minic_lp64_scalar_integer_width(program, target,
                    fn->values[term->conditional.condition].type,
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
