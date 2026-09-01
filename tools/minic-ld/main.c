#include "minild.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool has_archive_suffix(const char *path) {
    size_t length = strlen(path);

    return length >= 2U &&
           path[length - 2U] == '.' &&
           path[length - 1U] == 'a';
}


static void usage(FILE *out, const char *argv0) {
    fprintf(out,
            "usage: %s [-r|-static] -o OUTPUT [-e SYMBOL] "
            "[--whole-archive ARCHIVE --no-whole-archive] "
            "[--start-group ARCHIVE --end-group] INPUT...\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *output = NULL;
    const char *entry_symbol = "_start";
    MiniLdInput inputs[4096];
    size_t input_count = 0U;
    bool relocatable = false;
    bool static_link = false;
    bool whole_archive = false;
    bool group_mode = false;
    int i;

    if (argc == 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage(stdout, argv[0]);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("minic-ld 0.2");
        return 0;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-r") == 0) {
            relocatable = true;
        } else if (strcmp(argv[i], "-static") == 0) {
            static_link = true;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            output = argv[i];
        } else if (strcmp(argv[i], "--whole-archive") == 0) {
            whole_archive = true;
        } else if (strcmp(argv[i], "--no-whole-archive") == 0) {
            whole_archive = false;
        } else if (strcmp(argv[i], "--start-group") == 0 ||
                   strcmp(argv[i], "-(") == 0) {
            group_mode = true;
        } else if (strcmp(argv[i], "--end-group") == 0 ||
                   strcmp(argv[i], "-)") == 0) {
            group_mode = false;
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
            entry_symbol = argv[i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "minic-ld: unsupported-option:%s\n", argv[i]);
            return 2;
        } else {
            if (input_count == sizeof(inputs) / sizeof(inputs[0])) {
                fprintf(stderr, "minic-ld: too-many-inputs\n");
                return 2;
            }
            inputs[input_count].path = argv[i];
            if (whole_archive) {
                inputs[input_count].kind = MINILD_INPUT_WHOLE_ARCHIVE;
            } else if (group_mode) {
                inputs[input_count].kind = MINILD_INPUT_GROUP_ARCHIVE;
            } else if (has_archive_suffix(argv[i])) {
                inputs[input_count].kind = MINILD_INPUT_ARCHIVE;
            } else {
                inputs[input_count].kind = MINILD_INPUT_OBJECT;
            }
            ++input_count;
        }
    }

    if (output == NULL || input_count == 0U || whole_archive || group_mode) {
        usage(stderr, argv[0]);
        return 2;
    }

    if (relocatable) {
        return minild_link_relocatable_elf64_riscv_inputs(output,
                                                           inputs,
                                                           input_count,
                                                           stderr);
    }

    (void)static_link;
    return minild_link_static_elf64_riscv_inputs(output,
                                                  inputs,
                                                  input_count,
                                                  entry_symbol,
                                                  stderr);
}
