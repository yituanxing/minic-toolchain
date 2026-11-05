#include "target/machine_options/target_policies.h"

#include "target/machine_options/unsupported.h"
#include "target/x86_64/codegen_options.h"
#include "target/x86_64/machine_target_state.h"

#include <stdio.h>
#include <string.h>

bool minic_machine_options_x86_64_resolve(MinicTargetInfo *target,
                                          const char *march, const char *mabi,
                                          char *reason, size_t capacity) {
    (void)target;
    (void)march;
    (void)mabi;
    return minic_machine_options_reject_isa(reason, capacity);
}

bool minic_machine_options_x86_64_codegen(MinicTargetInfo *target,
                                          const char *option,
                                          char *reason, size_t capacity) {
    unsigned int flag = 0U;
    unsigned int flags = minic_x86_64_target_codegen_options(target);
    bool enabled = true;

    /* All x86 option spellings and their exact mitigation policy live here;
     * neither compiler driver nor target-neutral dispatch interprets them. */
    if (strcmp(option, "-fcf-protection=branch") == 0)
        flag = MINIC_X86_CODEGEN_IBT_ENDBR;
    else if (strcmp(option, "-fcf-protection=none") == 0) {
        flag = MINIC_X86_CODEGEN_IBT_ENDBR;
        enabled = false;
    } else if (strcmp(option, "-mindirect-branch=thunk-extern") == 0) {
        flags &=
            ~(unsigned int)MINIC_X86_CODEGEN_INDIRECT_INLINE_THUNK;
        flag = MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK;
    } else if (strcmp(option, "-mindirect-branch=thunk-inline") == 0) {
        flags &=
            ~(unsigned int)MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK;
        flag = MINIC_X86_CODEGEN_INDIRECT_INLINE_THUNK;
    } else if (strcmp(option, "-mindirect-branch=keep") == 0) {
        flag = MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK |
               MINIC_X86_CODEGEN_INDIRECT_INLINE_THUNK;
        enabled = false;
    } else if (strcmp(option, "-mindirect-branch-register") == 0) {
        /* Indirect CALL/computed goto already use a GP register. */
        return true;
    } else if (strcmp(option, "-mno-sse") == 0) {
        flag = MINIC_X86_CODEGEN_NO_SSE;
    } else if (strcmp(option, "-msse") == 0) {
        flag = MINIC_X86_CODEGEN_NO_SSE;
        enabled = false;
    } else if (strcmp(option, "-mfunction-return=thunk-extern") == 0)
        flag = MINIC_X86_CODEGEN_RETURN_EXTERNAL_THUNK;
    else if (strcmp(option, "-mfunction-return=keep") == 0) {
        flag = MINIC_X86_CODEGEN_RETURN_EXTERNAL_THUNK;
        enabled = false;
    } else if (strcmp(option, "-fpatchable-function-entry=16,16") == 0)
        flag = MINIC_X86_CODEGEN_PATCHABLE_ENTRY_16_16;
    else if (strcmp(option, "-fpatchable-function-entry=0,0") == 0 ||
             strcmp(option, "-fpatchable-function-entry=0") == 0) {
        flag = MINIC_X86_CODEGEN_PATCHABLE_ENTRY_16_16;
        enabled = false;
    } else {
        (void)snprintf(reason, capacity,
                       "unsupported x86 codegen option: %.160s", option);
        return false;
    }

    if (enabled) flags |= flag;
    else flags &= ~flag;
    return minic_x86_64_target_codegen_options_set(target, flags);
}
