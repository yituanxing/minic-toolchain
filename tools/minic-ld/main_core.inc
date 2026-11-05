#include "minild.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool has_archive_suffix(const char *path) {
    size_t length = strlen(path);

    return length >= 2U &&
           path[length - 2U] == '.' &&
           path[length - 1U] == 'a';
}

/* GNU's group rescan and whole-archive extraction are orthogonal flags.
 * Resolve them once for both explicit paths and -l search results. */
static MiniLdInputKind classify_input(const char *path, bool in_group,
                                     bool whole_archive) {
    if (in_group) {
        return whole_archive ? MINILD_INPUT_GROUP_WHOLE_ARCHIVE
                             : MINILD_INPUT_GROUP_ARCHIVE;
    }
    if (has_archive_suffix(path)) {
        return whole_archive ? MINILD_INPUT_WHOLE_ARCHIVE
                             : MINILD_INPUT_ARCHIVE;
    }
    return MINILD_INPUT_OBJECT;
}


static bool file_exists(const char *path) {
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        return false;
    }
    (void)fclose(file);
    return true;
}

static bool parse_page_size(const char *option, uint64_t *value_out) {
    const char prefix[] = "max-page-size=";
    char *end;
    unsigned long long value;
    if (strncmp(option, prefix, sizeof(prefix) - 1U) != 0) {
        return false;
    }
    errno = 0;
    value = strtoull(option + sizeof(prefix) - 1U, &end, 0);
    if (errno != 0 || end == option + sizeof(prefix) - 1U ||
        *end != '\0' || value < 4096ULL ||
        (value & (value - 1ULL)) != 0ULL || value > (1ULL << 30)) {
        return false;
    }
    *value_out = (uint64_t)value;
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
            "[-m EMULATION] [-EL|-EB] [-n|--nmagic] [-nostdlib] "
            "[--no-undefined] [-L DIR] [-l NAME] "
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
    const char *version_script_path = NULL;
    const char *needed_name = NULL;
    const char *interpreter_path = NULL;
    MiniLdInput inputs[4096];
    const char *library_dirs[256];
    const char *forced_undefined_symbols[256];
    size_t input_count = 0U;
    size_t input_path_count = 0U;
    size_t library_dir_count = 0U;
    size_t forced_undefined_count = 0U;
    bool relocatable = false;
    bool static_link = false;
    bool shared_link = false;
    bool pie_link = false;
    bool strip_debug = false;
    bool hash_style_both = false;
    bool eh_frame_hdr = false;
    bool whole_archive = false;
    bool group_mode = false;
    bool gc_sections = false;
    bool nmagic = false;
    bool discard_locals = false;
    bool no_dynamic_linker = false;
    bool emit_relocs = false;
    bool build_id_sha1 = false;
    bool z_notext = false;
    bool z_nodefaultlib = false;
    bool z_noexecstack = false;
    bool separate_code_explicit = false;
    bool separate_code = false;
    bool symbolic = false;
    bool no_apply_dynamic_relocs = false;
    bool pic_veneer = false;
    bool fix_cortex_a53_843419 = false;
    bool no_undefined = false;
    bool orphan_handling_error = false;
    uint64_t max_page_size = 0U;
    /* 0 = default target byte order; 1 = -EL, 2 = -EB. */
    unsigned int requested_endianness = 0U;
    bool kernel_static_pie;
    bool kernel_pie_image;
    bool kernel_resolved_pie;
    bool kernel_shared_image;
    bool kernel_et_dyn;
    const char *emulation = "elf64lriscv";
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
        } else if (strcmp(argv[i], "-u") == 0 ||
                   strcmp(argv[i], "--undefined") == 0 ||
                   (strncmp(argv[i], "-u", 2U) == 0 &&
                    argv[i][2] != '\0') ||
                   strncmp(argv[i], "--undefined=", 12U) == 0) {
            const char *name;
            size_t index;

            if (strcmp(argv[i], "-u") == 0 ||
                strcmp(argv[i], "--undefined") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "minic-ld: missing-forced-undefined-symbol\n");
                    return 2;
                }
                name = argv[i];
            } else if (strncmp(argv[i], "--undefined=", 12U) == 0) {
                name = argv[i] + 12U;
            } else {
                name = argv[i] + 2U;
            }
            if (*name == '\0' || *name == '-') {
                fprintf(stderr, "minic-ld: invalid-forced-undefined-symbol\n");
                return 2;
            }
            for (index = 0U; index < forced_undefined_count; ++index) {
                if (strcmp(forced_undefined_symbols[index], name) == 0) {
                    break;
                }
            }
            if (index == forced_undefined_count) {
                if (forced_undefined_count ==
                    sizeof(forced_undefined_symbols) /
                        sizeof(forced_undefined_symbols[0])) {
                    fprintf(stderr, "minic-ld: too-many-forced-undefined-symbols\n");
                    return 2;
                }
                forced_undefined_symbols[forced_undefined_count++] = name;
            }
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
            inputs[input_count].path = path;
            inputs[input_count].kind = classify_input(
                path, group_mode, whole_archive);
            ++input_count;
            ++input_path_count;
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
            if (needed_name != NULL) {
                fprintf(stderr, "minic-ld: multiple-needed-not-supported\n");
                return 2;
            }
            needed_name = argv[i];
            if (*needed_name == '\0') {
                fprintf(stderr, "minic-ld: empty-needed\n");
                return 2;
            }
        } else if (strncmp(argv[i], "--needed=", 9U) == 0) {
            if (needed_name != NULL) {
                fprintf(stderr, "minic-ld: multiple-needed-not-supported\n");
                return 2;
            }
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
        } else if (strcmp(argv[i], "--version-script") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            version_script_path = argv[i];
        } else if (strncmp(argv[i], "--version-script=", 17U) == 0) {
            version_script_path = argv[i] + 17U;
            if (*version_script_path == '\0') {
                fprintf(stderr, "minic-ld: empty-version-script\n");
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
        } else if (strcmp(argv[i], "--gc-sections") == 0) {
            gc_sections = true;
        } else if (strcmp(argv[i], "-n") == 0 ||
                   strcmp(argv[i], "--nmagic") == 0) {
            nmagic = true;
        } else if (strcmp(argv[i], "-nostdlib") == 0) {
            /*
             * GNU ld: inhibit searches of its built-in library paths.
             * MiniLD already resolves -l exclusively from explicit -L
             * directories, regardless of emulation and output mode.
             */
            continue;
        } else if (strcmp(argv[i], "--no-undefined") == 0) {
            no_undefined = true;
        } else if (strcmp(argv[i], "--no-dynamic-linker") == 0) {
            no_dynamic_linker = true;
        } else if (strcmp(argv[i], "--emit-relocs") == 0 ||
                   strcmp(argv[i], "-q") == 0) {
            emit_relocs = true;
        } else if (strcmp(argv[i], "--build-id=sha1") == 0 ||
                   strcmp(argv[i], "--build-id") == 0) {
            build_id_sha1 = true;
        } else if (strcmp(argv[i], "-Bsymbolic") == 0) {
            symbolic = true;
        } else if (strcmp(argv[i], "--no-apply-dynamic-relocs") == 0) {
            no_apply_dynamic_relocs = true;
        } else if (strcmp(argv[i], "--pic-veneer") == 0) {
            pic_veneer = true;
        } else if (strcmp(argv[i], "-X") == 0 ||
                   strcmp(argv[i], "--discard-locals") == 0) {
            discard_locals = true;
        } else if (strcmp(argv[i], "--discard-none") == 0) {
            /* GNU --discard-none overrides an earlier -X/-x selection. */
            discard_locals = false;
        } else if (strcmp(argv[i], "--fix-cortex-a53-843419") == 0) {
            fix_cortex_a53_843419 = true;
        } else if (strcmp(argv[i], "-EL") == 0 ||
                   strcmp(argv[i], "-EB") == 0) {
            unsigned int next = strcmp(argv[i], "-EL") == 0 ? 1U : 2U;
            if (requested_endianness != 0U && requested_endianness != next) {
                fprintf(stderr, "minic-ld: conflicting-endianness-options\n");
                return 2;
            }
            requested_endianness = next;
        } else if (strcmp(argv[i], "--no-whole-archive") == 0) {
            whole_archive = false;
        } else if (strcmp(argv[i], "--start-group") == 0 ||
                   strcmp(argv[i], "-(") == 0) {
            if (group_mode) {
                fprintf(stderr, "minic-ld: nested-archive-group\n");
                return 2;
            }
            group_mode = true;
        } else if (strcmp(argv[i], "--end-group") == 0 ||
                   strcmp(argv[i], "-)") == 0) {
            if (!group_mode) {
                fprintf(stderr, "minic-ld: unmatched-end-group\n");
                return 2;
            }
            group_mode = false;
            if (input_count == sizeof(inputs) / sizeof(inputs[0])) {
                fprintf(stderr, "minic-ld: too-many-inputs\n");
                return 2;
            }
            inputs[input_count].path = NULL;
            inputs[input_count].kind = MINILD_INPUT_GROUP_BREAK;
            ++input_count;
        } else if (strncmp(argv[i], "-m", 2U) == 0) {
            const char *requested_emulation = argv[i] + 2;
            if (*requested_emulation == '\0') {
                if (++i >= argc) {
                    usage(stderr, argv[0]);
                    return 2;
                }
                requested_emulation = argv[i];
            }
            /* GNU ld's -m is a stateful option: the last emulation wins.
             * Kbuild can pass its outer architecture -m followed by the
             * boot sublink's -m elf_i386; admitting both still requires
             * each input to match the final target ELF class/machine. */
            emulation = requested_emulation;
        } else if (strcmp(argv[i], "-z") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            if (strncmp(argv[i], "max-page-size=", 14U) == 0) {
                if (!parse_page_size(argv[i], &max_page_size)) {
                    fprintf(stderr, "minic-ld: invalid-max-page-size:%s\n",
                            argv[i]);
                    return 2;
                }
            } else if (strcmp(argv[i], "separate-code") == 0) {
                separate_code_explicit = true;
                separate_code = true;
            } else if (strcmp(argv[i], "noseparate-code") == 0) {
                separate_code_explicit = true;
                separate_code = false;
            } else if (strcmp(argv[i], "noexecstack") != 0 &&
                strcmp(argv[i], "nodefaultlib") != 0 &&
                strcmp(argv[i], "norelro") != 0 &&
                strcmp(argv[i], "notext") != 0) {
                fprintf(stderr, "minic-ld: unsupported-z:%s\n", argv[i]);
                return 2;
            }
            if (strcmp(argv[i], "nodefaultlib") == 0) {
                z_nodefaultlib = true;
            }
            if (strcmp(argv[i], "noexecstack") == 0) {
                z_noexecstack = true;
            }
            if (strcmp(argv[i], "notext") == 0) {
                z_notext = true;
            }
        } else if (strcmp(argv[i], "--orphan-handling=error") == 0) {
            orphan_handling_error = true;
        } else if (strcmp(argv[i], "--orphan-handling=warn") == 0) {
            orphan_handling_error = false;
        } else if (strcmp(argv[i], "--hash-style=both") == 0) {
            hash_style_both = true;
        } else if (strcmp(argv[i], "--eh-frame-hdr") == 0) {
            eh_frame_hdr = true;
        } else if (strcmp(argv[i], "-S") == 0 ||
                   strcmp(argv[i], "--strip-debug") == 0) {
            strip_debug = true;
        } else if (strcmp(argv[i], "--no-warn-rwx-segments") == 0 ||
                   strcmp(argv[i], "--no-ld-generated-unwind-info") == 0) {
            /*
             * The x86 compressed Linux link passes GNU's instruction not to
             * synthesize linker-generated unwind records. MiniLD currently
             * never synthesizes unwind records, so this is an exact no-op.
             * --no-warn-rwx-segments only suppresses a GNU diagnostic.
             * Neither option permits silently skipping an input/relocation.
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
            inputs[input_count].kind = classify_input(
                argv[i], group_mode, whole_archive);
            ++input_count;
            ++input_path_count;
        }
    }

    if (output == NULL || input_path_count == 0U ||
        whole_archive || group_mode) {
        usage(stderr, argv[0]);
        return 2;
    }

    /* GNU Kbuild uses -static -pie -n -T for a fixed-address kernel, but
     * x86 compressed/vmlinux is -pie --no-dynamic-linker -T WITHOUT -static.
     * Admit the latter only for a target expressly declaring freestanding
     * resolved kernel PIE, never by relaxing general user-space PIE modes. */
    kernel_static_pie = static_link && pie_link && !shared_link &&
                        !relocatable && script_path != NULL &&
                        no_dynamic_linker && interpreter_path == NULL;
    kernel_pie_image = pie_link && !static_link && !shared_link &&
                       !relocatable && script_path != NULL &&
                       no_dynamic_linker && !nmagic &&
                       minild_emulation_supports_kernel_pie_image(emulation) &&
                       interpreter_path == NULL && soname == NULL &&
                       version_script_path == NULL &&
                       dynamic_list_path == NULL && needed_name == NULL &&
                       !eh_frame_hdr && !hash_style_both && !strip_debug &&
                       !symbolic && !no_apply_dynamic_relocs && !pic_veneer;
    kernel_resolved_pie = kernel_static_pie || kernel_pie_image;
    /* Linux AArch64 uses -shared -Bsymbolic -T for a fully resolved kernel
     * ET_DYN, not the RV64 DSO/vDSO runtime with dynsym/DT_NEEDED. */
    kernel_shared_image = shared_link && !static_link && !pie_link &&
                          !relocatable && script_path != NULL &&
                          symbolic && no_apply_dynamic_relocs &&
                          minild_emulation_supports_kernel_shared_image(emulation) &&
                          interpreter_path == NULL &&
                          version_script_path == NULL && soname == NULL &&
                          needed_name == NULL && dynamic_list_path == NULL;
    kernel_et_dyn = kernel_shared_image ||
                    (kernel_static_pie && !nmagic) || kernel_pie_image;
    /* Do not parse GNU options and then silently drop them at a mode
     * dispatcher which has no matching Options contract.  These modes may
     * gain support later, but must never emit a misleading successful ELF. */
    if (relocatable &&
        (entry_symbol != NULL || script_path != NULL || strip_debug ||
         build_id_sha1 || orphan_handling_error)) {
        fprintf(stderr, "minic-ld: unsupported-relocatable-link-options\n");
        return 2;
    }
    if ((soname != NULL || needed_name != NULL ||
         dynamic_list_path != NULL) &&
        (relocatable || (!shared_link && !pie_link))) {
        fprintf(stderr, "minic-ld: shared-options-require-shared-or-pie\n");
        return 2;
    }
    if (build_id_sha1 &&
        (relocatable || (shared_link && !kernel_shared_image) ||
         (pie_link && !kernel_resolved_pie))) {
        fprintf(stderr, "minic-ld: build-id-requires-final-static-link\n");
        return 2;
    }
    if (z_nodefaultlib &&
        ((shared_link && !kernel_shared_image) ||
         (pie_link && !kernel_resolved_pie))) {
        fprintf(stderr, "minic-ld: unsupported-dynamic-nodefaultlib\n");
        return 2;
    }
    if (z_noexecstack &&
        ((shared_link && !kernel_shared_image) ||
         (pie_link && !kernel_resolved_pie))) {
        /* PT_GNU_STACK is currently owned by neither dynamic program plan.
         * Do not pretend to honor an explicit non-executable-stack request. */
        fprintf(stderr, "minic-ld: unsupported-dynamic-noexecstack\n");
        return 2;
    }
    if (emit_relocs && (relocatable || shared_link || pie_link ||
                        strcmp(emulation, "elf_x86_64") != 0)) {
        fprintf(stderr, "minic-ld: --emit-relocs-requires-x86-static-link\n");
        return 2;
    }
    if ((symbolic || no_apply_dynamic_relocs || pic_veneer) &&
        !kernel_shared_image) {
        fprintf(stderr, "minic-ld: kernel-shared-options-require-static-ET_DYN-script\n");
        return 2;
    }
    if (fix_cortex_a53_843419 &&
        ((strcmp(emulation, "aarch64linux") != 0 &&
          strcmp(emulation, "aarch64elf") != 0) ||
         relocatable || pie_link ||
         (shared_link && !kernel_shared_image))) {
        fprintf(stderr, "minic-ld: --fix-cortex-a53-843419-requires-aarch64-static-or-kernel-image\n");
        return 2;
    }

    if ((relocatable ? 1 : 0) + (shared_link ? 1 : 0) +
            (pie_link ? 1 : 0) >
        1 ||
        (static_link && pie_link && !kernel_static_pie)) {
        fprintf(stderr, "minic-ld: incompatible-link-modes\n");
        return 2;
    }
    if (forced_undefined_count != 0U &&
        (relocatable ||
         (shared_link && !kernel_shared_image) ||
         (pie_link && !kernel_resolved_pie))) {
        fprintf(stderr,
                "minic-ld: -u-requires-final-static-link\n");
        return 2;
    }
    if (version_script_path != NULL && !shared_link) {
        fprintf(stderr, "minic-ld: version-script-requires-shared\n");
        return 2;
    }
    if (eh_frame_hdr && !shared_link && !pie_link) {
        fprintf(stderr, "minic-ld: eh-frame-hdr-requires-shared-or-pie\n");
        return 2;
    }
    if (hash_style_both && !shared_link && !pie_link) {
        fprintf(stderr, "minic-ld: hash-style-both-requires-shared-or-pie\n");
        return 2;
    }
    if (interpreter_path != NULL && !pie_link) {
        fprintf(stderr, "minic-ld: dynamic-linker-requires-pie\n");
        return 2;
    }
    if (no_dynamic_linker &&
        (relocatable || shared_link ||
         (pie_link && !kernel_resolved_pie) || interpreter_path != NULL)) {
        fprintf(stderr, "minic-ld: --no-dynamic-linker-requires-static-link\n");
        return 2;
    }
    if (orphan_handling_error && script_path == NULL) {
        fprintf(stderr, "minic-ld: --orphan-handling=error-requires-script\n");
        return 2;
    }
    if (max_page_size != 0U && (relocatable ||
        (shared_link && !kernel_shared_image) ||
        (pie_link && !kernel_resolved_pie))) {
        fprintf(stderr, "minic-ld: -z max-page-size-requires-final-kernel-or-static-link\n");
        return 2;
    }
    if (separate_code_explicit && (relocatable ||
        (shared_link && !kernel_shared_image) ||
        (pie_link && !kernel_resolved_pie))) {
        fprintf(stderr,
                "minic-ld: -z separate-code-requires-final-kernel-or-static-link\n");
        return 2;
    }
    if (discard_locals && (relocatable ||
                           (shared_link && !kernel_shared_image) ||
                           (pie_link && !kernel_resolved_pie))) {
        fprintf(stderr, "minic-ld: --discard-locals-requires-static-link\n");
        return 2;
    }
    if (gc_sections && (relocatable ||
                        (shared_link && !kernel_shared_image) ||
                        (pie_link && !kernel_resolved_pie))) {
        fprintf(stderr,
                "minic-ld: --gc-sections is currently supported only for final static links\n");
        return 2;
    }
    if (nmagic && (relocatable || shared_link ||
                   (pie_link && !kernel_resolved_pie) ||
                   script_path == NULL)) {
        fprintf(stderr,
                "minic-ld: --nmagic-requires-static-script\n");
        return 2;
    }
    if (no_undefined && (relocatable ||
                         (shared_link && !kernel_shared_image) ||
                         (pie_link && !kernel_resolved_pie))) {
        /*
         * Static final links already reject unresolved strong references
         * (while allowing undefined weak references to resolve to zero).
         * Other link modes need their own explicit --no-undefined policy;
         * rejecting them prevents accidentally accepting a weaker contract.
         */
        fprintf(stderr,
                "minic-ld: --no-undefined-requires-final-static-link\n");
        return 2;
    }
    /*
     * A shared ELF link must honor -T. Previously the CLI parsed the
     * script and silently discarded it in the shared/PIE path, which could
     * produce an apparently successful but unbootable Linux vDSO missing
     * script-defined symbols (e.g. _vdso_data). Fail closed until the
     * shared-script layout and symbol evaluator are fully implemented.
     * Existing static linker-script handling is unchanged.
     */
    if (pie_link && script_path != NULL && !kernel_resolved_pie) {
        fprintf(stderr, "minic-ld: unsupported-pie-linker-script:%s\n",
                script_path);
        return 2;
    }
    if (z_notext && !kernel_resolved_pie && !kernel_shared_image) {
        fprintf(stderr, "minic-ld: -z notext-requires-kernel-link-mode\n");
        return 2;
    }
    if (shared_link && !kernel_shared_image && script_path != NULL &&
        version_script_path != NULL) {
        fprintf(stderr, "minic-ld: ambiguous-version-and-layout-script\n");
        return 2;
    }

    {
        MiniLdLinkMode mode = relocatable ? MINILD_LINK_RELOCATABLE :
                              shared_link && !kernel_shared_image
                                  ? MINILD_LINK_SHARED :
                              pie_link && !kernel_resolved_pie
                                  ? MINILD_LINK_PIE : MINILD_LINK_STATIC;
        if (!minild_emulation_supports_mode(emulation, mode)) {
            fprintf(stderr, "minic-ld: unsupported-emulation-or-link-mode:%s\n",
                    emulation);
            return 2;
        }
        if (requested_endianness != 0U &&
            !minild_emulation_supports_endianness(
                emulation, requested_endianness == 1U)) {
            fprintf(stderr, "minic-ld: unsupported-target-endianness:%s:%s\n",
                    emulation,
                    requested_endianness == 1U ? "little" : "big");
            return 2;
        }
    }
    if (relocatable) {
        return minild_link_relocatable_inputs_target(
            output, inputs, input_count, emulation, stderr);
    }
    if ((shared_link && !kernel_shared_image) ||
        (pie_link && !kernel_resolved_pie)) {
        MiniLdSharedOptions options;

        options.soname = soname;
        options.entry_symbol = entry_symbol;
        options.dynamic_list_path = dynamic_list_path;
        options.version_script_path = version_script_path;
        options.script_path = script_path;
        options.needed_name = needed_name;
        options.interpreter_path = interpreter_path;
        options.strip_debug = strip_debug;
        options.hash_style_both = hash_style_both;
        options.eh_frame_hdr = eh_frame_hdr;
        options.pie = pie_link;
        return minild_link_shared_inputs_target(
            output, inputs, input_count, &options, emulation, stderr);
    }

    {
        MiniLdStaticOptions options;

        (void)static_link;
        options.entry_symbol = entry_symbol;
        options.script_path = script_path;
        options.forced_undefined_symbols = forced_undefined_symbols;
        options.forced_undefined_count = forced_undefined_count;
        options.gc_sections = gc_sections;
        options.nmagic = nmagic;
        options.discard_locals = discard_locals;
        options.strip_debug = strip_debug;
        options.output_et_dyn = kernel_et_dyn;
        options.static_pie_requested = kernel_resolved_pie;
        options.fix_cortex_a53_843419 = fix_cortex_a53_843419;
        options.max_page_size = max_page_size;
        options.separate_code_explicit = separate_code_explicit;
        options.separate_code = separate_code;
        options.orphan_handling_error = orphan_handling_error;
        options.kernel_shared_no_dynamic_relocations = kernel_shared_image;
        options.emit_relocs = emit_relocs;
        options.build_id_sha1 = build_id_sha1;
        return minild_link_static_inputs_target(
            output, inputs, input_count, &options, emulation, stderr);
    }
}
