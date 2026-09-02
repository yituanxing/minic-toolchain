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
    bool strip_all;
    bool binary_output;
} ObjcopyOptions;

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
            "usage: %s -O binary [-R SECTION] [-S] INPUT OUTPUT\n",
            program);
}

static bool append_remove(ObjcopyOptions *options, const char *name) {
    const char **next;

    if (options->remove_count == options->remove_capacity) {
        size_t capacity =
            options->remove_capacity == 0U ? 8U : options->remove_capacity * 2U;
        if (capacity < options->remove_capacity ||
            capacity > SIZE_MAX / sizeof(*options->remove_sections)) {
            return false;
        }
        next = realloc(options->remove_sections,
                       capacity * sizeof(*options->remove_sections));
        if (next == NULL) {
            return false;
        }
        options->remove_sections = next;
        options->remove_capacity = capacity;
    }
    options->remove_sections[options->remove_count++] = name;
    return true;
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

    if (!read_file(input_path, &input, &input_size)) {
        goto done;
    }
    if (!minielf_open(&elf, input, input_size) ||
        (elf.type != ET_EXEC && elf.type != ET_DYN && elf.type != ET_REL)) {
        fprintf(stderr,
                "minic-objcopy: %s: unsupported file format\n",
                input_path);
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
            free(options.remove_sections);
            return 0;
        }
        if (strcmp(argument, "--version") == 0) {
            puts("minic-objcopy 0.1");
            free(options.remove_sections);
            return 0;
        }
        if (strcmp(argument, "-O") == 0) {
            if (++i >= argc || strcmp(argv[i], "binary") != 0) {
                fprintf(stderr,
                        "minic-objcopy: M0 supports only '-O binary'\n");
                free(options.remove_sections);
                return 2;
            }
            options.binary_output = true;
            continue;
        }
        if (strncmp(argument, "--output-target=", 16U) == 0) {
            if (strcmp(argument + 16U, "binary") != 0) {
                fprintf(stderr,
                        "minic-objcopy: M0 supports only binary output\n");
                free(options.remove_sections);
                return 2;
            }
            options.binary_output = true;
            continue;
        }
        if (strcmp(argument, "-R") == 0 ||
            strcmp(argument, "--remove-section") == 0) {
            if (++i >= argc || !append_remove(&options, argv[i])) {
                fprintf(stderr,
                        "minic-objcopy: invalid remove-section option\n");
                free(options.remove_sections);
                return 2;
            }
            continue;
        }
        if (strncmp(argument, "--remove-section=", 17U) == 0) {
            if (!append_remove(&options, argument + 17U)) {
                fprintf(stderr, "minic-objcopy: out of memory\n");
                free(options.remove_sections);
                return 2;
            }
            continue;
        }
        if (strcmp(argument, "-S") == 0 ||
            strcmp(argument, "--strip-all") == 0) {
            options.strip_all = true;
            continue;
        }
        if (argument[0] == '-') {
            fprintf(stderr,
                    "minic-objcopy: unsupported option: %s\n",
                    argument);
            free(options.remove_sections);
            return 2;
        }
        if (input_path == NULL) {
            input_path = argument;
        } else if (output_path == NULL) {
            output_path = argument;
        } else {
            usage(stderr, argv[0]);
            free(options.remove_sections);
            return 2;
        }
    }

    if (!options.binary_output || input_path == NULL || output_path == NULL) {
        usage(stderr, argv[0]);
        free(options.remove_sections);
        return 2;
    }

    result = convert_binary(&options, input_path, output_path);
    free(options.remove_sections);
    return result;
}
