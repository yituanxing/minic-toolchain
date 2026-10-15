#include "minielf.h"

#include <elf.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct StripOptions {
    bool strip_all;
    bool strip_debug;
    const char *output_path;
} StripOptions;

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
            "usage: %s [-s|--strip-all] [-g|--strip-debug] [-o FILE] INPUT\n",
            program);
}

static bool read_file(const char *path,
                      unsigned char **data_out,
                      size_t *size_out) {
    FILE *file = fopen(path, "rb");
    long end;
    size_t size;
    unsigned char *data;

    if (file == NULL) {
        fprintf(stderr, "minic-strip: %s: %s\n", path, strerror(errno));
        return false;
    }
    if (fseek(file, 0L, SEEK_END) != 0 ||
        (end = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fprintf(stderr,
                "minic-strip: %s: cannot determine file size\n",
                path);
        fclose(file);
        return false;
    }
    size = (size_t)end;
    data = malloc(size == 0U ? 1U : size);
    if (data == NULL) {
        fprintf(stderr, "minic-strip: %s: out of memory\n", path);
        fclose(file);
        return false;
    }
    if (size != 0U && fread(data, 1U, size, file) != size) {
        fprintf(stderr, "minic-strip: %s: read error\n", path);
        free(data);
        fclose(file);
        return false;
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "minic-strip: %s: close error\n", path);
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
        fprintf(stderr, "minic-strip: %s: %s\n", path, strerror(errno));
        return false;
    }
    if ((size == 0U || fwrite(data, 1U, size, file) == size) &&
        fflush(file) == 0) {
        ok = true;
    } else {
        fprintf(stderr, "minic-strip: %s: write error\n", path);
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        (void)remove(path);
    }
    return ok;
}

static bool preserve_mode(const char *input_path, const char *output_path) {
    struct stat status;

    if (stat(input_path, &status) != 0) {
        fprintf(stderr,
                "minic-strip: %s: cannot read file mode: %s\n",
                input_path,
                strerror(errno));
        return false;
    }
    if (chmod(output_path, status.st_mode & 07777U) != 0) {
        fprintf(stderr,
                "minic-strip: %s: cannot preserve file mode: %s\n",
                output_path,
                strerror(errno));
        return false;
    }
    return true;
}

static bool write_output(const char *input_path,
                         const char *output_path,
                         const unsigned char *data,
                         size_t size) {
    bool in_place = strcmp(input_path, output_path) == 0;
    char *temporary = NULL;
    const char *target = output_path;
    size_t length;
    bool ok = false;

    if (in_place) {
        length = strlen(input_path);
        if (length > SIZE_MAX - sizeof(".minic-strip.tmp")) {
            fprintf(stderr, "minic-strip: path is too long\n");
            return false;
        }
        temporary = malloc(length + sizeof(".minic-strip.tmp"));
        if (temporary == NULL) {
            fprintf(stderr, "minic-strip: out of memory\n");
            return false;
        }
        memcpy(temporary, input_path, length);
        memcpy(temporary + length,
               ".minic-strip.tmp",
               sizeof(".minic-strip.tmp"));
        target = temporary;
    }

    if (!write_file(target, data, size) ||
        !preserve_mode(input_path, target)) {
        goto done;
    }
    if (in_place && rename(target, input_path) != 0) {
        fprintf(stderr,
                "minic-strip: %s: cannot replace input: %s\n",
                input_path,
                strerror(errno));
        goto done;
    }
    ok = true;

done:
    if (!ok && temporary != NULL) {
        (void)remove(temporary);
    }
    free(temporary);
    return ok;
}

static int strip_elf(const StripOptions *options,
                     const char *input_path,
                     const char *output_path) {
    unsigned char *input = NULL;
    size_t input_size = 0U;
    MiniElfView elf;
    MiniElfRewriteOptions rewrite_options;
    MiniElfRewriteError error = MINIELF_REWRITE_OK;
    unsigned char *image = NULL;
    size_t image_size = 0U;
    int result = 1;

    if (!read_file(input_path, &input, &input_size)) {
        goto done;
    }
    if (!minielf_open(&elf, input, input_size) ||
        (elf.type != ET_EXEC && elf.type != ET_DYN)) {
        fprintf(stderr,
                "minic-strip: %s: M0 supports ELF executables/shared objects only\n",
                input_path);
        goto done;
    }

    memset(&rewrite_options, 0, sizeof(rewrite_options));
    rewrite_options.strip_all = options->strip_all;
    rewrite_options.strip_debug = options->strip_debug;

    if (!minielf_rewrite(&elf,
                         &rewrite_options,
                         &image,
                         &image_size,
                         &error)) {
        fprintf(stderr,
                "minic-strip: %s: ELF rewrite failed: %s\n",
                input_path,
                minielf_rewrite_error_string(error));
        goto done;
    }
    if (!write_output(input_path, output_path, image, image_size)) {
        goto done;
    }
    result = 0;

done:
    free(image);
    free(input);
    return result;
}

int main(int argc, char **argv) {
    StripOptions options;
    const char *input_path = NULL;
    const char *output_path;
    int i;

    memset(&options, 0, sizeof(options));

    for (i = 1; i < argc; ++i) {
        const char *argument = argv[i];

        if (strcmp(argument, "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        }
        if (strcmp(argument, "--version") == 0) {
            puts("minic-strip 0.1");
            return 0;
        }
        if (strcmp(argument, "-s") == 0 ||
            strcmp(argument, "--strip-all") == 0) {
            options.strip_all = true;
            continue;
        }
        if (strcmp(argument, "-g") == 0 ||
            strcmp(argument, "--strip-debug") == 0) {
            options.strip_debug = true;
            continue;
        }
        if (strcmp(argument, "-o") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "minic-strip: missing output file after -o\n");
                return 2;
            }
            options.output_path = argv[i];
            continue;
        }
        if (strncmp(argument, "--output=", 9U) == 0) {
            if (argument[9] == '\0') {
                fprintf(stderr, "minic-strip: empty output file\n");
                return 2;
            }
            options.output_path = argument + 9U;
            continue;
        }
        if (argument[0] == '-') {
            fprintf(stderr,
                    "minic-strip: unsupported option: %s\n",
                    argument);
            return 2;
        }
        if (input_path != NULL) {
            usage(stderr, argv[0]);
            return 2;
        }
        input_path = argument;
    }

    if (input_path == NULL) {
        usage(stderr, argv[0]);
        return 2;
    }
    if (!options.strip_all && !options.strip_debug) {
        options.strip_all = true;
    }

    output_path =
        options.output_path != NULL ? options.output_path : input_path;
    return strip_elf(&options, input_path, output_path);
}
