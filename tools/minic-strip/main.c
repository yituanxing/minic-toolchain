#include "minielf.h"

#include <elf.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct StripOptions {
    bool strip_debug;
    bool strip_all;
    const char *output_path;
} StripOptions;

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
            "usage: %s [options] FILE...\n"
            "  -g, -d, --strip-debug    remove debug sections\n"
            "  -s, --strip-all          remove static symbols/debug from EXEC/DYN\n"
            "  -o FILE                  write one input to FILE\n",
            program);
}

static bool read_file(const char *path,
                      unsigned char **data_out,
                      size_t *size_out,
                      mode_t *mode_out) {
    struct stat status;
    FILE *file;
    long end;
    size_t size;
    unsigned char *data;

    if (stat(path, &status) != 0) {
        fprintf(stderr, "minic-strip: %s: %s\n", path, strerror(errno));
        return false;
    }
    file = fopen(path, "rb");
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
    *mode_out = status.st_mode;
    return true;
}

static bool write_file(const char *path,
                       const unsigned char *data,
                       size_t size,
                       mode_t mode) {
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
    if (ok && chmod(path, mode & 07777) != 0) {
        fprintf(stderr, "minic-strip: %s: chmod: %s\n",
                path,
                strerror(errno));
        ok = false;
    }
    if (!ok) {
        (void)remove(path);
    }
    return ok;
}

static char *temporary_path(const char *path) {
    static const char suffix[] = ".minic-strip.tmp";
    size_t length = strlen(path);
    char *result;

    if (length > SIZE_MAX - sizeof(suffix)) {
        return NULL;
    }
    result = malloc(length + sizeof(suffix));
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, path, length);
    memcpy(result + length, suffix, sizeof(suffix));
    return result;
}

static int strip_one(const StripOptions *options,
                     const char *input_path,
                     const char *output_path) {
    unsigned char *input = NULL;
    size_t input_size = 0U;
    mode_t mode = 0;
    MiniElfView elf;
    MiniElfRewriteOptions rewrite_options;
    MiniElfRewriteError error = MINIELF_REWRITE_OK;
    unsigned char *image = NULL;
    size_t image_size = 0U;
    char *temp = NULL;
    const char *write_path = output_path;
    bool in_place = strcmp(input_path, output_path) == 0;
    int result = 1;

    if (!read_file(input_path, &input, &input_size, &mode)) {
        goto done;
    }
    if (!minielf_open(&elf, input, input_size) ||
        (elf.type != ET_REL && elf.type != ET_EXEC && elf.type != ET_DYN)) {
        fprintf(stderr,
                "minic-strip: %s: unsupported file format\n",
                input_path);
        goto done;
    }

    memset(&rewrite_options, 0, sizeof(rewrite_options));
    rewrite_options.strip_debug = options->strip_debug;
    rewrite_options.strip_all = options->strip_all;

    if (!minielf_rewrite(&elf,
                         &rewrite_options,
                         &image,
                         &image_size,
                         &error)) {
        fprintf(stderr,
                "minic-strip: %s: rewrite failed: %s\n",
                input_path,
                minielf_rewrite_error_string(error));
        goto done;
    }

    if (in_place) {
        temp = temporary_path(output_path);
        if (temp == NULL) {
            fprintf(stderr, "minic-strip: out of memory\n");
            goto done;
        }
        write_path = temp;
    }
    if (!write_file(write_path, image, image_size, mode)) {
        goto done;
    }
    if (in_place && rename(write_path, output_path) != 0) {
        fprintf(stderr,
                "minic-strip: %s: rename: %s\n",
                output_path,
                strerror(errno));
        (void)remove(write_path);
        goto done;
    }
    result = 0;

done:
    free(temp);
    free(image);
    free(input);
    return result;
}

int main(int argc, char **argv) {
    StripOptions options;
    int first_file = 1;
    int i;
    int file_count;
    int failures = 0;

    memset(&options, 0, sizeof(options));

    while (first_file < argc) {
        const char *argument = argv[first_file];

        if (strcmp(argument, "--") == 0) {
            ++first_file;
            break;
        }
        if (argument[0] != '-' || argument[1] == '\0') {
            break;
        }
        if (strcmp(argument, "--strip-debug") == 0 ||
            strcmp(argument, "-g") == 0 ||
            strcmp(argument, "-d") == 0) {
            options.strip_debug = true;
        } else if (strcmp(argument, "--strip-all") == 0 ||
                   strcmp(argument, "-s") == 0) {
            options.strip_all = true;
        } else if (strcmp(argument, "-o") == 0) {
            if (++first_file >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            options.output_path = argv[first_file];
        } else if (strcmp(argument, "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(argument, "--version") == 0) {
            puts("minic-strip 0.1");
            return 0;
        } else {
            fprintf(stderr,
                    "minic-strip: unsupported option: %s\n",
                    argument);
            return 2;
        }
        ++first_file;
    }

    if (first_file >= argc) {
        usage(stderr, argv[0]);
        return 2;
    }
    if (!options.strip_debug && !options.strip_all) {
        options.strip_all = true;
    }

    file_count = argc - first_file;
    if (options.output_path != NULL && file_count != 1) {
        fprintf(stderr,
                "minic-strip: -o requires exactly one input file\n");
        return 2;
    }

    for (i = first_file; i < argc; ++i) {
        const char *output =
            options.output_path != NULL ? options.output_path : argv[i];

        if (strip_one(&options, argv[i], output) != 0) {
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
