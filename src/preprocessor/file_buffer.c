#define _GNU_SOURCE

#include "minic/preprocessor.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void reset_buffer(MinicPpFileBuffer *buffer) {
    buffer->path = NULL;
    buffer->bytes = NULL;
    buffer->length = 0U;
}

int minic_pp_file_buffer_load(const char *path, MinicPpFileBuffer *out_buffer) {
    FILE *file;
    long end_position;
    size_t length;
    unsigned char *bytes;
    char *canonical_path;

    if (path == NULL || out_buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    reset_buffer(out_buffer);
    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        (void)fclose(file);
        return -1;
    }
    end_position = ftell(file);
    if (end_position < 0L || (uintmax_t)end_position > (uintmax_t)SIZE_MAX) {
        (void)fclose(file);
        errno = EOVERFLOW;
        return -1;
    }
    length = (size_t)end_position;
    if (length == SIZE_MAX) {
        (void)fclose(file);
        errno = EOVERFLOW;
        return -1;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return -1;
    }

    bytes = malloc(length + 1U);
    if (bytes == NULL) {
        (void)fclose(file);
        return -1;
    }
    if (length != 0U && fread(bytes, 1U, length, file) != length) {
        free(bytes);
        (void)fclose(file);
        return -1;
    }
    bytes[length] = 0U;

    if (fclose(file) != 0) {
        free(bytes);
        return -1;
    }

    canonical_path = realpath(path, NULL);
    if (canonical_path == NULL) {
        free(bytes);
        return -1;
    }

    out_buffer->path = canonical_path;
    out_buffer->bytes = bytes;
    out_buffer->length = length;
    return 0;
}

void minic_pp_file_buffer_destroy(MinicPpFileBuffer *buffer) {
    if (buffer == NULL) {
        return;
    }
    free(buffer->path);
    free(buffer->bytes);
    reset_buffer(buffer);
}
