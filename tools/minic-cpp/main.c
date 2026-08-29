#include "minipp.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *output, const char *argv0) {
    fprintf(output,
            "usage: %s -E -P -undef -nostdinc "
            "[-DNAME[=VALUE]] [-UNAME] [-IDIR] -o OUTPUT INPUT.c\n",
            argv0);
}

int main(int argc, char **argv) {
    MiniPpConfig config;
    const char *input = NULL;
    const char *output = NULL;
    const char **defines;
    const char **undefines;
    const char **include_paths;
    size_t define_count = 0U;
    size_t undefine_count = 0U;
    size_t include_path_count = 0U;
    int index;
    int status;

    memset(&config, 0, sizeof(config));
    defines = calloc((size_t)argc, sizeof(*defines));
    undefines = calloc((size_t)argc, sizeof(*undefines));
    include_paths = calloc((size_t)argc, sizeof(*include_paths));
    if (defines == NULL || undefines == NULL || include_paths == NULL) {
        fprintf(stderr, "minic-cpp: out-of-memory\n");
        free(defines);
        free(undefines);
        free(include_paths);
        return 1;
    }

    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];

        if (strcmp(argument, "-E") == 0) {
            continue;
        }
        if (strcmp(argument, "-P") == 0) {
            config.suppress_line_markers = true;
            continue;
        }
        if (strcmp(argument, "-undef") == 0) {
            config.inhibit_predefined_macros = true;
            continue;
        }
        if (strcmp(argument, "-nostdinc") == 0) {
            config.no_standard_includes = true;
            continue;
        }
        if (strcmp(argument, "-o") == 0) {
            if (++index >= argc) {
                usage(stderr, argv[0]);
                status = 2;
                goto done;
            }
            output = argv[index];
            continue;
        }
        if (strcmp(argument, "-x") == 0) {
            if (++index >= argc || strcmp(argv[index], "c") != 0) {
                fprintf(stderr, "minic-cpp: only-x-c-is-supported\n");
                status = 2;
                goto done;
            }
            continue;
        }
        if (strncmp(argument, "-D", 2U) == 0) {
            if (argument[2] != '\0') {
                defines[define_count++] = argument + 2;
            } else if (++index < argc) {
                defines[define_count++] = argv[index];
            } else {
                usage(stderr, argv[0]);
                status = 2;
                goto done;
            }
            continue;
        }
        if (strncmp(argument, "-U", 2U) == 0) {
            if (argument[2] != '\0') {
                undefines[undefine_count++] = argument + 2;
            } else if (++index < argc) {
                undefines[undefine_count++] = argv[index];
            } else {
                usage(stderr, argv[0]);
                status = 2;
                goto done;
            }
            continue;
        }
        if (strncmp(argument, "-I", 2U) == 0) {
            if (argument[2] != '\0') {
                include_paths[include_path_count++] = argument + 2;
            } else if (++index < argc) {
                include_paths[include_path_count++] = argv[index];
            } else {
                usage(stderr, argv[0]);
                status = 2;
                goto done;
            }
            continue;
        }
        if (strcmp(argument, "-h") == 0 ||
            strcmp(argument, "--help") == 0) {
            usage(stdout, argv[0]);
            status = 0;
            goto done;
        }
        if (argument[0] == '-') {
            fprintf(stderr, "minic-cpp: unsupported-option:%s\n", argument);
            status = 2;
            goto done;
        }
        if (input != NULL) {
            fprintf(stderr, "minic-cpp: multiple-inputs\n");
            status = 2;
            goto done;
        }
        input = argument;
    }

    if (input == NULL || output == NULL) {
        usage(stderr, argv[0]);
        status = 2;
        goto done;
    }

    config.defines = defines;
    config.define_count = define_count;
    config.undefines = undefines;
    config.undefine_count = undefine_count;
    config.include_paths = include_paths;
    config.include_path_count = include_path_count;
    status = minipp_preprocess_file(input, output, &config, stderr);

done:
    free(defines);
    free(undefines);
    free(include_paths);
    return status;
}
