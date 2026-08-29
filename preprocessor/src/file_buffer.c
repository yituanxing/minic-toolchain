#include "minipp_internal.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void minipp_string_init(MiniPpString *string) {
    string->data = NULL;
    string->size = 0U;
    string->capacity = 0U;
}

void minipp_string_destroy(MiniPpString *string) {
    free(string->data);
    minipp_string_init(string);
}

static bool minipp_string_reserve(MiniPpString *string, size_t required) {
    size_t capacity;
    char *next;

    if (required <= string->capacity) {
        return true;
    }

    capacity = string->capacity == 0U ? 256U : string->capacity;
    while (capacity < required) {
        if (capacity > (SIZE_MAX / 2U)) {
            return false;
        }
        capacity *= 2U;
    }

    next = realloc(string->data, capacity);
    if (next == NULL) {
        return false;
    }
    string->data = next;
    string->capacity = capacity;
    return true;
}

bool minipp_string_append_n(MiniPpString *string,
                            const char *data,
                            size_t size) {
    if (size == 0U) {
        return true;
    }
    if (string->size > SIZE_MAX - size - 1U) {
        return false;
    }
    if (!minipp_string_reserve(string, string->size + size + 1U)) {
        return false;
    }
    memcpy(string->data + string->size, data, size);
    string->size += size;
    string->data[string->size] = '\0';
    return true;
}

bool minipp_string_append_char(MiniPpString *string, char value) {
    return minipp_string_append_n(string, &value, 1U);
}

bool minipp_read_file(const char *path, MiniPpString *out, FILE *diagnostics) {
    FILE *input;
    char buffer[4096];

    input = fopen(path, "rb");
    if (input == NULL) {
        fprintf(diagnostics,
                "minic-cpp: cannot-open-input:%s:%s\n",
                path,
                strerror(errno));
        return false;
    }

    for (;;) {
        size_t count = fread(buffer, 1U, sizeof(buffer), input);
        if (count != 0U && !minipp_string_append_n(out, buffer, count)) {
            fprintf(diagnostics, "minic-cpp: out-of-memory\n");
            fclose(input);
            return false;
        }
        if (count != sizeof(buffer)) {
            if (ferror(input) != 0) {
                fprintf(diagnostics,
                        "minic-cpp: read-error:%s:%s\n",
                        path,
                        strerror(errno));
                fclose(input);
                return false;
            }
            break;
        }
    }

    fclose(input);
    return true;
}

bool minipp_write_file(const char *path,
                       const char *data,
                       size_t size,
                       FILE *diagnostics) {
    FILE *output;

    output = fopen(path, "wb");
    if (output == NULL) {
        fprintf(diagnostics,
                "minic-cpp: cannot-open-output:%s:%s\n",
                path,
                strerror(errno));
        return false;
    }
    if (size != 0U && fwrite(data, 1U, size, output) != size) {
        fprintf(diagnostics,
                "minic-cpp: write-error:%s:%s\n",
                path,
                strerror(errno));
        fclose(output);
        return false;
    }
    if (fclose(output) != 0) {
        fprintf(diagnostics,
                "minic-cpp: close-error:%s:%s\n",
                path,
                strerror(errno));
        return false;
    }
    return true;
}
