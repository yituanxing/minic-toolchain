#include "minias.h"
#include "minias_internal.h"

#include <string.h>

/* The public API is intentionally independent of MiniAsTargetOps. All
 * ISA/ABI parsing, bitness and target-specific ELF policy is resolved at
 * the one registry/TargetSpec boundary before the MC engine is created. */
int minias_assemble_invocation(const char *input_path,
                               const char *output_path,
                               const MiniAsInvocation *invocation,
                               FILE *diagnostic) {
    const char *target_name = "riscv";
    const char *march = NULL;
    const char *mabi = NULL;
    const MiniAsTargetOps *target;
    MiniAsAssemblyConfig config;
    char error[256] = {0};

    if (invocation != NULL) {
        if (invocation->target != NULL) target_name = invocation->target;
        march = invocation->march;
        mabi = invocation->mabi;
    }
    if (input_path == NULL || output_path == NULL) {
        if (diagnostic != NULL)
            fprintf(diagnostic, "minic-as: missing-input-or-output\n");
        return 2;
    }
    target = minias_find_target(target_name);
    if (target == NULL) {
        if (diagnostic != NULL)
            fprintf(diagnostic, "minic-as: unsupported-target:%s\n", target_name);
        return 2;
    }
    memset(&config, 0, sizeof(config));
    if (target->resolve_profile == NULL ||
        !target->resolve_profile(march, mabi, &config,
                                 error, sizeof(error))) {
        if (diagnostic != NULL)
            fprintf(diagnostic, "minic-as: %s\n",
                    error[0] != '\0' ? error : "missing-target-profile");
        return 2;
    }
    if (config.target != target) {
        if (diagnostic != NULL)
            fprintf(diagnostic, "minic-as: internal:profile-target-mismatch\n");
        return 2;
    }
    return minias_assemble_config(input_path, output_path, &config, diagnostic);
}
