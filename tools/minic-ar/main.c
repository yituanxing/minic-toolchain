#include "miniar.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void usage(FILE *out, const char *argv0) {
    fprintf(out,
            "usage: %s [-]r[c][s|S][T][P][D|U] ARCHIVE [MEMBER ...]\n",
            argv0);
}

int main(int argc, char **argv) {
    MiniArOptions options = {false, true, false, false};
    const char *flags;
    bool replace = false;
    size_t i;

    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(stdout, argv[0]);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("minic-ar 0.1");
        return 0;
    }
    if (argc < 3) {
        usage(stderr, argv[0]);
        return 2;
    }

    flags = argv[1];
    if (*flags == '-') {
        ++flags;
    }
    for (i = 0U; flags[i] != '\0'; ++i) {
        switch (flags[i]) {
        case 'r':
            replace = true;
            break;
        case 'c':
            break;
        case 's':
            options.write_index = true;
            break;
        case 'S':
            options.write_index = false;
            break;
        case 'T':
            options.thin = true;
            break;
        case 'P':
            options.preserve_paths = true;
            break;
        case 'D':
            options.deterministic = true;
            break;
        case 'U':
            options.deterministic = false;
            break;
        default:
            fprintf(stderr, "minic-ar: unsupported-option:%c\n", flags[i]);
            return 2;
        }
    }
    if (!replace) {
        fprintf(stderr, "minic-ar: A0 supports replace/create operation 'r' only\n");
        return 2;
    }

    return miniar_create_archive(argv[2],
                                 (const char *const *)&argv[3],
                                 (size_t)(argc - 3),
                                 &options,
                                 stderr);
}
