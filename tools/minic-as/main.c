#include "minias.h"
#include <stdio.h>
#include <string.h>

static void usage(FILE *out, const char *name) {
    size_t i;
    fprintf(out, "usage: %s [--target=", name);
    for (i = 0U; i < minias_supported_target_count(); ++i) {
        const char *target = minias_supported_target_name(i);
        if (target == NULL) continue;
        if (i != 0U) fputc('|', out);
        fputs(target, out);
    }
    fputs("] [-march=ISA] [-mabi=ABI] -o OUTPUT INPUT.s\n", out);
}

int main(int argc, char **argv) {
    const char *input = NULL, *output = NULL;
    const char *march = NULL, *mabi = NULL, *target_name = "riscv";
    MiniAsInvocation invocation;
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

    invocation.target = target_name;
    invocation.march = march;
    invocation.mabi = mabi;
    return minias_assemble_invocation(input, output, &invocation, stderr);
}
