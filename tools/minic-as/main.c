#include "minias.h"

#include <stdio.h>
#include <string.h>

static void usage(FILE *out, const char *argv0) {
    fprintf(out,
            "usage: %s [-march=rv32...|-march=rv64...] "
            "[-mabi=ilp32...|-mabi=lp64...] -o OUTPUT INPUT.s\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *output = NULL;
    bool elf32 = false;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            output = argv[i];
        } else if (strncmp(argv[i], "-march=", 7U) == 0) {
            const char *arch = argv[i] + 7;
            if (strncmp(arch, "rv32", 4U) == 0) {
                elf32 = true;
            } else if (strncmp(arch, "rv64", 4U) == 0) {
                elf32 = false;
            } else {
                fprintf(stderr, "minic-as: unsupported-arch:%s\n", arch);
                return 2;
            }
        } else if (strncmp(argv[i], "-mabi=", 6U) == 0) {
            const char *abi = argv[i] + 6;
            if (strncmp(abi, "ilp32", 5U) == 0) {
                elf32 = true;
            } else if (strncmp(abi, "lp64", 4U) == 0) {
                elf32 = false;
            } else {
                fprintf(stderr, "minic-as: unsupported-abi:%s\n", abi);
                return 2;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
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
    return minias_assemble_file_class(input, output, elf32, stderr);
}
