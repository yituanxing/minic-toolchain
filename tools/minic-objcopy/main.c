#include "minielf.h"

#include <elf.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ObjcopyOptions {
    const char **remove_sections;
    size_t remove_count;
    size_t remove_capacity;
    const char **keep_globals;
    size_t keep_global_count;
    size_t keep_global_capacity;
    bool strip_all;
    bool strip_debug;
    bool binary_output;
} ObjcopyOptions;

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
            "usage: %s [options] INPUT OUTPUT\n"
            "  -O binary                 write a flat binary image\n"
            "  -R SECTION                remove SECTION\n"
            "  -S, --strip-all           strip the static symbol/debug tables\n"
            "      --strip-debug         remove debug sections\n"
            "  -G SYMBOL                 keep SYMBOL global, localize other definitions\n",
            program);
}

static bool append_string(const char ***items,
                          size_t *count,
                          size_t *capacity,
                          const char *value) {
    const char **next;

    if (*count == *capacity) {
        size_t next_capacity =
            *capacity == 0U ? 8U : *capacity * 2U;

        if (next_capacity < *capacity ||
            next_capacity > SIZE_MAX / sizeof(**items)) {
            return false;
        }
        next = realloc(*items, next_capacity * sizeof(**items));
        if (next == NULL) {
            return false;
        }
        *items = next;
        *capacity = next_capacity;
    }
    (*items)[(*count)++] = value;
    return true;
}

static bool append_remove(ObjcopyOptions *options, const char *name) {
    return append_string(&options->remove_sections,
                         &options->remove_count,
                         &options->remove_capacity,
                         name);
}

static bool append_keep_global(ObjcopyOptions *options,
                               const char *name) {
    return append_string(&options->keep_globals,
                         &options->keep_global_count,
                         &options->keep_global_capacity,
                         name);
}

static bool is_removed(const ObjcopyOptions *options, const char *name) {
    size_t i;

    for (i = 0U; i < options->remove_count; ++i) {
        if (strcmp(options->remove_sections[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static bool read_file(const char *path,
                      unsigned char **data_out,
                      size_t *size_out) {
    FILE *file = fopen(path, "rb");
    long end;
    size_t size;
    unsigned char *data;

    if (file == NULL) {
        fprintf(stderr, "minic-objcopy: %s: %s\n", path, strerror(errno));
        return false;
    }
    if (fseek(file, 0L, SEEK_END) != 0 ||
        (end = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fprintf(stderr,
                "minic-objcopy: %s: cannot determine file size\n",
                path);
        fclose(file);
        return false;
    }
    size = (size_t)end;
    data = malloc(size == 0U ? 1U : size);
    if (data == NULL) {
        fprintf(stderr, "minic-objcopy: %s: out of memory\n", path);
        fclose(file);
        return false;
    }
    if (size != 0U && fread(data, 1U, size, file) != size) {
        fprintf(stderr, "minic-objcopy: %s: read error\n", path);
        free(data);
        fclose(file);
        return false;
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "minic-objcopy: %s: close error\n", path);
        free(data);
        return false;
    }
    *data_out = data;
    *size_out = size;
    return true;
}

static bool write_file(const char *path,
                       const unsigned char *data,
                       size_t size) {
    FILE *file = fopen(path, "wb");
    bool ok = false;

    if (file == NULL) {
        fprintf(stderr, "minic-objcopy: %s: %s\n", path, strerror(errno));
        return false;
    }
    if ((size == 0U || fwrite(data, 1U, size, file) == size) &&
        fflush(file) == 0) {
        ok = true;
    } else {
        fprintf(stderr, "minic-objcopy: %s: write error\n", path);
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        (void)remove(path);
    }
    return ok;
}

static int open_elf(const char *input_path,
                    unsigned char **input_out,
                    size_t *input_size_out,
                    MiniElfView *elf_out) {
    unsigned char *input = NULL;
    size_t input_size = 0U;

    if (!read_file(input_path, &input, &input_size)) {
        return 1;
    }
    if (!minielf_open(elf_out, input, input_size) ||
        (elf_out->type != ET_EXEC &&
         elf_out->type != ET_DYN &&
         elf_out->type != ET_REL)) {
        fprintf(stderr,
                "minic-objcopy: %s: unsupported file format\n",
                input_path);
        free(input);
        return 1;
    }
    *input_out = input;
    *input_size_out = input_size;
    return 0;
}

static bool build_remove_map(const ObjcopyOptions *options,
                             const MiniElfView *elf,
                             bool **remove_out) {
    bool *remove;
    size_t i;

    remove = calloc(elf->section_count == 0U ? 1U : elf->section_count,
                    sizeof(*remove));
    if (remove == NULL) {
        return false;
    }
    for (i = 1U; i < elf->section_count; ++i) {
        const char *name;

        if (!minielf_section_name(elf, i, &name)) {
            free(remove);
            return false;
        }
        remove[i] = is_removed(options, name);
    }
    *remove_out = remove;
    return true;
}

static int convert_binary(const ObjcopyOptions *options,
                          const char *input_path,
                          const char *output_path) {
    unsigned char *input = NULL;
    size_t input_size = 0U;
    MiniElfView elf;
    bool *include_sections = NULL;
    unsigned char *image = NULL;
    size_t image_size = 0U;
    uint64_t base_address = 0U;
    MiniElfBinaryError error = MINIELF_BINARY_OK;
    size_t i;
    int result = 1;

    (void)options->strip_all;
    (void)options->strip_debug;
    (void)options->keep_globals;
    (void)options->keep_global_count;

    if (open_elf(input_path, &input, &input_size, &elf) != 0) {
        goto done;
    }
    include_sections =
        calloc(elf.section_count == 0U ? 1U : elf.section_count,
               sizeof(*include_sections));
    if (include_sections == NULL) {
        fprintf(stderr, "minic-objcopy: out of memory\n");
        goto done;
    }

    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection section;
        const char *name;

        if (!minielf_section(&elf, i, &section) ||
            !minielf_section_name(&elf, i, &name)) {
            fprintf(stderr,
                    "minic-objcopy: %s: invalid section table\n",
                    input_path);
            goto done;
        }
        if ((section.flags & SHF_ALLOC) == 0U ||
            section.type == SHT_NOBITS ||
            section.size == 0U ||
            is_removed(options, name)) {
            continue;
        }
        include_sections[i] = true;
    }

    if (!minielf_build_binary(&elf,
                              include_sections,
                              &image,
                              &image_size,
                              &base_address,
                              &error)) {
        fprintf(stderr,
                "minic-objcopy: %s: binary export failed: %s\n",
                input_path,
                minielf_binary_error_string(error));
        goto done;
    }
    (void)base_address;

    if (!write_file(output_path, image, image_size)) {
        goto done;
    }
    result = 0;

done:
    free(image);
    free(include_sections);
    free(input);
    return result;
}

static int rewrite_elf(const ObjcopyOptions *options,
                       const char *input_path,
                       const char *output_path) {
    unsigned char *input = NULL;
    size_t input_size = 0U;
    MiniElfView elf;
    bool *remove = NULL;
    MiniElfRewriteOptions rewrite_options;
    MiniElfRewriteError error = MINIELF_REWRITE_OK;
    unsigned char *image = NULL;
    size_t image_size = 0U;
    int result = 1;

    if (open_elf(input_path, &input, &input_size, &elf) != 0) {
        goto done;
    }
    if (elf.type == ET_REL) {
        fprintf(stderr,
                "minic-objcopy: %s: ELF-to-ELF ET_REL rewrite is not in A1\n",
                input_path);
        goto done;
    }
    if (!build_remove_map(options, &elf, &remove)) {
        fprintf(stderr,
                "minic-objcopy: %s: cannot build section filter\n",
                input_path);
        goto done;
    }

    memset(&rewrite_options, 0, sizeof(rewrite_options));
    rewrite_options.remove_sections = remove;
    rewrite_options.strip_all = options->strip_all;
    rewrite_options.strip_debug = options->strip_debug;
    rewrite_options.keep_global_symbols = options->keep_globals;
    rewrite_options.keep_global_count = options->keep_global_count;

    if (!minielf_rewrite(&elf,
                         &rewrite_options,
                         &image,
                         &image_size,
                         &error)) {
        fprintf(stderr,
                "minic-objcopy: %s: ELF rewrite failed: %s\n",
                input_path,
                minielf_rewrite_error_string(error));
        goto done;
    }
    if (!write_file(output_path, image, image_size)) {
        goto done;
    }
    result = 0;

done:
    free(image);
    free(remove);
    free(input);
    return result;
}

static bool parse_value_option(int argc,
                               char **argv,
                               int *index,
                               const char *argument,
                               const char *long_prefix,
                               const char **value_out) {
    size_t prefix_length = strlen(long_prefix);

    if (strncmp(argument, long_prefix, prefix_length) == 0) {
        *value_out = argument + prefix_length;
        return **value_out != '\0';
    }
    if (++(*index) >= argc) {
        return false;
    }
    *value_out = argv[*index];
    return true;
}

int main(int argc, char **argv) {
    ObjcopyOptions options;
    const char *input_path = NULL;
    const char *output_path = NULL;
    int i;
    int result;

    memset(&options, 0, sizeof(options));

    for (i = 1; i < argc; ++i) {
        const char *argument = argv[i];

        if (strcmp(argument, "--help") == 0) {
            usage(stdout, argv[0]);
            result = 0;
            goto done;
        }
        if (strcmp(argument, "--version") == 0) {
            puts("minic-objcopy 0.2");
            result = 0;
            goto done;
        }
        if (strcmp(argument, "-O") == 0) {
            if (++i >= argc || strcmp(argv[i], "binary") != 0) {
                fprintf(stderr,
                        "minic-objcopy: A1 supports only '-O binary' target conversion\n");
                result = 2;
                goto done;
            }
            options.binary_output = true;
            continue;
        }
        if (strncmp(argument, "--output-target=", 16U) == 0) {
            if (strcmp(argument + 16U, "binary") != 0) {
                fprintf(stderr,
                        "minic-objcopy: A1 supports only binary target conversion\n");
                result = 2;
                goto done;
            }
            options.binary_output = true;
            continue;
        }
        if (strcmp(argument, "-R") == 0 ||
            strcmp(argument, "--remove-section") == 0) {
            const char *value;

            if (!parse_value_option(argc,
                                    argv,
                                    &i,
                                    argument,
                                    "--remove-section=",
                                    &value) ||
                !append_remove(&options, value)) {
                fprintf(stderr,
                        "minic-objcopy: invalid remove-section option\n");
                result = 2;
                goto done;
            }
            continue;
        }
        if (strncmp(argument, "--remove-section=", 17U) == 0) {
            if (!append_remove(&options, argument + 17U)) {
                fprintf(stderr, "minic-objcopy: out of memory\n");
                result = 2;
                goto done;
            }
            continue;
        }
        if (strcmp(argument, "-G") == 0 ||
            strcmp(argument, "--keep-global-symbol") == 0) {
            if (++i >= argc ||
                !append_keep_global(&options, argv[i])) {
                fprintf(stderr,
                        "minic-objcopy: invalid keep-global-symbol option\n");
                result = 2;
                goto done;
            }
            continue;
        }
        if (strncmp(argument, "--keep-global-symbol=", 21U) == 0) {
            if (!append_keep_global(&options, argument + 21U)) {
                fprintf(stderr, "minic-objcopy: out of memory\n");
                result = 2;
                goto done;
            }
            continue;
        }
        if (strcmp(argument, "-S") == 0 ||
            strcmp(argument, "--strip-all") == 0) {
            options.strip_all = true;
            continue;
        }
        if (strcmp(argument, "--strip-debug") == 0) {
            options.strip_debug = true;
            continue;
        }
        if (argument[0] == '-') {
            fprintf(stderr,
                    "minic-objcopy: unsupported option: %s\n",
                    argument);
            result = 2;
            goto done;
        }
        if (input_path == NULL) {
            input_path = argument;
        } else if (output_path == NULL) {
            output_path = argument;
        } else {
            usage(stderr, argv[0]);
            result = 2;
            goto done;
        }
    }

    if (input_path == NULL || output_path == NULL) {
        usage(stderr, argv[0]);
        result = 2;
        goto done;
    }

    result = options.binary_output
                 ? convert_binary(&options, input_path, output_path)
                 : rewrite_elf(&options, input_path, output_path);

done:
    free(options.keep_globals);
    free(options.remove_sections);
    return result;
}
