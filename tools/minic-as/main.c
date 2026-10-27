#include "minias.h"
#include "minias_internal.h"
#include <stdio.h>
#include <string.h>

static void usage(FILE *out, const char *name) {
    fprintf(out, "usage: %s [--target=riscv|aarch64] "
            "[-march=ISA] [-mabi=ABI] -o OUTPUT INPUT.s\n", name);
}

int main(int argc, char **argv) {
    const char *input = NULL, *output = NULL;
    const char *march = NULL, *mabi = NULL, *target_name = "riscv";
    const MiniAsTargetOps *target;
    MiniAsAssemblyConfig config;
    char error[256] = {0};
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) { usage(stderr, argv[0]); return 2; }
            output = argv[i];
        } else if (strncmp(argv[i], "-march=", 7U) == 0) {
            march = argv[i] + 7U;
        } else if (strncmp(argv[i], "-mabi=", 6U) == 0) {
            mabi = argv[i] + 6U;
        } else if (strncmp(argv[i], "--target=", 9U) == 0) {
            target_name = argv[i] + 9U;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "minic-as: unsupported-option:%s\n", argv[i]);
            return 2;
        } else if (input == NULL) {
            input = argv[i];
        } else {
            fprintf(stderr, "minic-as: multiple-inputs\n");
            return 2;
        }
    }
    if (input == NULL || output == NULL) {
        usage(stderr, argv[0]);
        return 2;
    }

    target = minias_find_target(target_name);
    if (target == NULL) {
        fprintf(stderr, "minic-as: unsupported-target:%s\n", target_name);
        return 2;
    }
    if (target->resolve_profile == NULL ||
        !target->resolve_profile(march, mabi, &config,
                                 error, sizeof(error))) {
        fprintf(stderr, "minic-as: %s\n",
                error[0] != '\0' ? error : "missing-target-profile");
        return 2;
    }
    if (config.target != target) {
        fprintf(stderr, "minic-as: internal:profile-target-mismatch\n");
        return 2;
    }
    return minias_assemble_config(input, output, &config, stderr);
}
