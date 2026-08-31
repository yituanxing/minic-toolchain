#include "minild.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void usage(FILE *out, const char *argv0) {
    fprintf(out, "usage: %s -r -o OUTPUT INPUT.o [INPUT.o ...]\n", argv0);
}

int main(int argc, char **argv) {
    const char *output = NULL;
    const char *inputs[1024];
    size_t input_count = 0U;
    bool relocatable = false;
    int i;

    if (argc == 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage(stdout, argv[0]);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("minic-ld 0.1");
        return 0;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-r") == 0) {
            relocatable = true;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            output = argv[i];
        } else if (strncmp(argv[i], "-m", 2U) == 0) {
            const char *emulation = argv[i] + 2;
            if (*emulation == '\0') {
                if (++i >= argc) {
                    usage(stderr, argv[0]);
                    return 2;
                }
                emulation = argv[i];
            }
            if (strcmp(emulation, "elf64lriscv") != 0) {
                fprintf(stderr, "minic-ld: unsupported-emulation:%s\n", emulation);
                return 2;
            }
        } else if (strcmp(argv[i], "-z") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            if (strcmp(argv[i], "noexecstack") != 0 &&
                strcmp(argv[i], "nodefaultlib") != 0) {
                fprintf(stderr, "minic-ld: unsupported-z:%s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--no-warn-rwx-segments") == 0) {
            continue;
        } else if (strcmp(argv[i], "-e") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            /*
             * Entry selection has no effect on an ET_REL output. Accept it
             * because Linux purgatory passes -e together with -r.
             */
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "minic-ld: unsupported-option:%s\n", argv[i]);
            return 2;
        } else {
            if (input_count == sizeof(inputs) / sizeof(inputs[0])) {
                fprintf(stderr, "minic-ld: too-many-inputs\n");
                return 2;
            }
            inputs[input_count++] = argv[i];
        }
    }

    if (!relocatable || output == NULL || input_count == 0U) {
        usage(stderr, argv[0]);
        return 2;
    }

    return minild_link_relocatable_elf64_riscv(output,
                                                inputs,
                                                input_count,
                                                stderr);
}
