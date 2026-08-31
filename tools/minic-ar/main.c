#include "miniar.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void usage(FILE *out, const char *argv0) {
    fprintf(out,
            "usage: %s [-]r[c][s|S][T][P][D|U] ARCHIVE [MEMBER ...]\n"
            "       %s [-]t ARCHIVE\n",
            argv0,
            argv0);
}

int main(int argc, char **argv) {
    MiniArOptions options = {false, true, false, false};
    const char *flags;
    bool replace = false;
    bool list = false;
    bool move = false;
    bool insert_before = false;
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
        case 't':
            list = true;
            break;
        case 'm':
            move = true;
            break;
        case 'i':
            insert_before = true;
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
    if ((replace ? 1 : 0) + (list ? 1 : 0) + (move ? 1 : 0) != 1) {
        fprintf(stderr, "minic-ar: A1 requires exactly one operation: r, t, or m\n");
        return 2;
    }
    if (list) {
        if (argc != 3) {
            fprintf(stderr, "minic-ar: A1 list does not accept member filters\n");
            return 2;
        }
        return miniar_list_archive(argv[2], stdout, stderr);
    }


    if (move) {
        FILE *archive;

        if (!insert_before) {
            fprintf(stderr, "minic-ar: A1 move requires modifier i\n");
            return 2;
        }
        if (argc < 4) {
            usage(stderr, argv[0]);
            return 2;
        }
        if (argc > 4) {
            fprintf(stderr, "minic-ar: A1 move with explicit members is not yet supported\n");
            return 2;
        }

        /*
         * Linux Kbuild may invoke:
         *   ar mPiT RELPOS ARCHIVE
         * with no members after ARCHIVE. GNU ar treats this as a successful
         * no-op when the archive already exists. Preserve that exact boundary.
         */
        archive = fopen(argv[3], "rb");
        if (archive != NULL) {
            fclose(archive);
            return 0;
        }

        return miniar_create_archive(argv[3],
                                     NULL,
                                     0U,
                                     &options,
                                     stderr);
    }

    return miniar_create_archive(argv[2],
                                 (const char *const *)&argv[3],
                                 (size_t)(argc - 3),
                                 &options,
                                 stderr);
}
