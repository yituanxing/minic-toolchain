#include "minild.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool has_archive_suffix(const char *path) {
    size_t length = strlen(path);

    return length >= 2U &&
           path[length - 2U] == '.' &&
           path[length - 1U] == 'a';
}


static bool file_exists(const char *path) {
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        return false;
    }
    (void)fclose(file);
    return true;
}

static char *library_candidate(const char *directory,
                               const char *name,
                               const char *suffix) {
    size_t directory_size = strlen(directory);
    size_t name_size = strlen(name);
    size_t suffix_size = strlen(suffix);
    bool slash = directory_size != 0U &&
                 directory[directory_size - 1U] != '/';
    size_t total;
    char *path;

    if (directory_size > SIZE_MAX - name_size - suffix_size - 5U) {
        return NULL;
    }
    total = directory_size + (slash ? 1U : 0U) +
            3U + name_size + suffix_size + 1U;
    path = malloc(total);
    if (path == NULL) {
        return NULL;
    }
    (void)snprintf(path,
                   total,
                   "%s%slib%s%s",
                   directory,
                   slash ? "/" : "",
                   name,
                   suffix);
    return path;
}

static char *find_library(const char *const *directories,
                          size_t directory_count,
                          const char *name,
                          bool static_only) {
    size_t i;

    for (i = 0U; i < directory_count; ++i) {
        char *candidate;

        if (!static_only) {
            candidate = library_candidate(directories[i], name, ".so");
            if (candidate == NULL) {
                return NULL;
            }
            if (file_exists(candidate)) {
                return candidate;
            }
            free(candidate);
        }

        candidate = library_candidate(directories[i], name, ".a");
        if (candidate == NULL) {
            return NULL;
        }
        if (file_exists(candidate)) {
            return candidate;
        }
        free(candidate);
    }
    return NULL;
}

static void usage(FILE *out, const char *argv0) {
    fprintf(out,
            "usage: %s [-r|-static|-shared|-pie] -o OUTPUT [-e SYMBOL] [-soname NAME] "
            "[--dynamic-list FILE] [--needed NAME] [--dynamic-linker PATH] "
            "[-L DIR] [-l NAME] "
            "[--whole-archive ARCHIVE --no-whole-archive] "
            "[--start-group ARCHIVE --end-group] INPUT...\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *output = NULL;
    const char *entry_symbol = NULL;
    const char *script_path = NULL;
    const char *soname = NULL;
    const char *dynamic_list_path = NULL;
    const char *needed_name = NULL;
    const char *interpreter_path = NULL;
    MiniLdInput inputs[4096];
    const char *library_dirs[256];
    char *owned_library_inputs[4096];
    size_t input_count = 0U;
    size_t library_dir_count = 0U;
    size_t owned_library_input_count = 0U;
    bool relocatable = false;
    bool static_link = false;
    bool shared_link = false;
    bool pie_link = false;
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
        const char *directory = NULL;

        if (strcmp(argv[i], "-L") == 0) {
            if (i + 1 >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            directory = argv[++i];
        } else if (strncmp(argv[i], "-L", 2U) == 0 &&
                   argv[i][2] != '\0') {
            directory = argv[i] + 2U;
        }
        if (directory != NULL) {
            if (*directory == '\0' ||
                library_dir_count ==
                    sizeof(library_dirs) / sizeof(library_dirs[0])) {
                fprintf(stderr, "minic-ld: invalid-library-path\n");
                return 2;
            }
            library_dirs[library_dir_count++] = directory;
        }
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-r") == 0) {
            relocatable = true;
        } else if (strcmp(argv[i], "-static") == 0) {
            static_link = true;
        } else if (strcmp(argv[i], "-shared") == 0) {
            shared_link = true;
        } else if (strcmp(argv[i], "-pie") == 0) {
            pie_link = true;
        } else if (strcmp(argv[i], "-L") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
        } else if (strncmp(argv[i], "-L", 2U) == 0 &&
                   argv[i][2] != '\0') {
            continue;
        } else if (strcmp(argv[i], "-l") == 0 ||
                   (strncmp(argv[i], "-l", 2U) == 0 &&
                    argv[i][2] != '\0')) {
            const char *name;
            char *path;

            if (strcmp(argv[i], "-l") == 0) {
                if (++i >= argc) {
                    usage(stderr, argv[0]);
                    return 2;
                }
                name = argv[i];
            } else {
                name = argv[i] + 2U;
            }
            if (*name == '\0' ||
                input_count == sizeof(inputs) / sizeof(inputs[0])) {
                fprintf(stderr, "minic-ld: invalid-library:%s\n", name);
                return 2;
            }
            path = find_library(library_dirs,
                                library_dir_count,
                                name,
                                static_link);
            if (path == NULL) {
                fprintf(stderr,
                        "minic-ld: cannot-find-library:%s\n",
                        name);
                return 2;
            }
            owned_library_inputs[owned_library_input_count++] = path;
            inputs[input_count].path = path;
            inputs[input_count].kind =
                has_archive_suffix(path)
                    ? (whole_archive ? MINILD_INPUT_WHOLE_ARCHIVE
                                     : MINILD_INPUT_ARCHIVE)
                    : MINILD_INPUT_OBJECT;
            ++input_count;
        } else if (strcmp(argv[i], "--dynamic-linker") == 0 ||
                   strcmp(argv[i], "-dynamic-linker") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            interpreter_path = argv[i];
            if (*interpreter_path == '\0') {
                fprintf(stderr, "minic-ld: empty-dynamic-linker\n");
                return 2;
            }
        } else if (strncmp(argv[i], "--dynamic-linker=", 17U) == 0) {
            interpreter_path = argv[i] + 17U;
            if (*interpreter_path == '\0') {
                fprintf(stderr, "minic-ld: empty-dynamic-linker\n");
                return 2;
            }
        } else if (strcmp(argv[i], "-soname") == 0 ||
                   strcmp(argv[i], "--soname") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            soname = argv[i];
        } else if (strncmp(argv[i], "-soname=", 8U) == 0) {
            soname = argv[i] + 8U;
        } else if (strncmp(argv[i], "--soname=", 9U) == 0) {
            soname = argv[i] + 9U;
        } else if (strcmp(argv[i], "--needed") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            needed_name = argv[i];
            if (*needed_name == '\0') {
                fprintf(stderr, "minic-ld: empty-needed\n");
                return 2;
            }
        } else if (strncmp(argv[i], "--needed=", 9U) == 0) {
            needed_name = argv[i] + 9U;
            if (*needed_name == '\0') {
                fprintf(stderr, "minic-ld: empty-needed\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--dynamic-list") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            dynamic_list_path = argv[i];
        } else if (strncmp(argv[i], "--dynamic-list=", 15U) == 0) {
            dynamic_list_path = argv[i] + 15U;
            if (*dynamic_list_path == '\0') {
                fprintf(stderr, "minic-ld: empty-dynamic-list\n");
                return 2;
            }
        } else if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            output = argv[i];
        } else if (strncmp(argv[i], "--script=", 9U) == 0) {
            script_path = argv[i] + 9U;
            if (*script_path == '\0') {
                fprintf(stderr, "minic-ld: empty-linker-script\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--script") == 0 ||
                   strcmp(argv[i], "-T") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            script_path = argv[i];
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
                strcmp(argv[i], "nodefaultlib") != 0 &&
                strcmp(argv[i], "norelro") != 0) {
                fprintf(stderr, "minic-ld: unsupported-z:%s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--no-warn-rwx-segments") == 0 ||
                   strcmp(argv[i], "--strip-debug") == 0 ||
                   strcmp(argv[i], "--build-id=sha1") == 0 ||
                   strcmp(argv[i], "--orphan-handling=warn") == 0) {
            /*
             * These options affect ELF metadata/debug retention or diagnostics,
             * not the A2/A3 static load image. Accept the Linux final-link
             * spellings now; dedicated metadata emission can refine them later.
             */
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
            if (group_mode) {
                /*
                 * GNU --start-group applies to the complete input sequence,
                 * including ordinary .o files. Objects appearing after an
                 * archive may introduce new unresolved symbols that require
                 * an earlier archive to be rescanned.
                 */
                inputs[input_count].kind = MINILD_INPUT_GROUP_ARCHIVE;
            } else if (has_archive_suffix(argv[i])) {
                if (whole_archive) {
                    inputs[input_count].kind = MINILD_INPUT_WHOLE_ARCHIVE;
                } else {
                    inputs[input_count].kind = MINILD_INPUT_ARCHIVE;
                }
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

    if ((relocatable ? 1 : 0) + (shared_link ? 1 : 0) +
            (pie_link ? 1 : 0) >
        1 ||
        (static_link && pie_link)) {
        fprintf(stderr, "minic-ld: incompatible-link-modes\n");
        return 2;
    }
    if (interpreter_path != NULL && !pie_link) {
        fprintf(stderr, "minic-ld: dynamic-linker-requires-pie\n");
        return 2;
    }

    if (relocatable) {
        return minild_link_relocatable_elf64_riscv_inputs(output,
                                                           inputs,
                                                           input_count,
                                                           stderr);
    }
    if (shared_link || pie_link) {
        MiniLdSharedOptions options;

        options.soname = soname;
        options.entry_symbol = entry_symbol;
        options.dynamic_list_path = dynamic_list_path;
        options.needed_name = needed_name;
        options.interpreter_path = interpreter_path;
        options.pie = pie_link;
        return minild_link_shared_elf64_riscv_inputs_options(output,
                                                              inputs,
                                                              input_count,
                                                              &options,
                                                              stderr);
    }

    {
        MiniLdStaticOptions options;

        (void)static_link;
        options.entry_symbol = entry_symbol;
        options.script_path = script_path;
        return minild_link_static_elf64_riscv_inputs_options(output,
                                                              inputs,
                                                              input_count,
                                                              &options,
                                                              stderr);
    }
}
