#include "minias.h"
#include "minias_internal.h"
#include "riscv_profile.h"
#include <stdio.h>
#include <string.h>

static void usage(FILE *out, const char *name) {
    fprintf(out, "usage: %s [--target=riscv|aarch64] "
            "[-march=ISA] [-mabi=ABI] -o OUTPUT INPUT.s\n", name);
}

int main(int argc, char **argv) {
    const char *input = NULL, *output = NULL;
    const char *march = NULL, *mabi = NULL, *target = "riscv";
    const MiniAsTargetOps *backend;
    MiniAsRiscvProfile profile;
    char error[256];
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
            target = argv[i] + 9U;
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
    if (input == NULL || output == NULL) { usage(stderr, argv[0]); return 2; }

    if (strcmp(target, "aarch64") == 0) {
        if ((march != NULL && strcmp(march, "armv8-a") != 0) ||
            (mabi != NULL && strcmp(mabi, "lp64") != 0)) {
            fprintf(stderr, "minic-as: unsupported-aarch64-isa-abi\n");
            return 2;
        }
        backend = minias_target_aarch64();
        return minias_assemble_with_target(input, output, backend,
                                           false, 0U, 0U, stderr);
    }
    if (strcmp(target, "riscv") != 0) {
        fprintf(stderr, "minic-as: unsupported-target:%s\n", target);
        return 2;
    }
    if (!minias_riscv_resolve_profile(march, mabi, &profile,
                                     error, sizeof(error))) {
        fprintf(stderr, "minic-as: %s\n", error);
        return 2;
    }
    backend = minias_target_riscv();
    return minias_assemble_with_target(input, output, backend, profile.elf32,
                                       profile.elf_flags,
                                       profile.strict_i_xlen, stderr);
}
