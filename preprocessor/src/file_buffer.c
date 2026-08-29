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

bool minipp_splice_backslash_newlines(const MiniPpString *input,
                                      MiniPpString *output) {
    size_t index = 0U;

    minipp_string_init(output);
    while (index < input->size) {
        if (input->data[index] == '\\' && index + 1U < input->size) {
            if (input->data[index + 1U] == '\n') {
                index += 2U;
                continue;
            }
            if (input->data[index + 1U] == '\r' &&
                index + 2U < input->size &&
                input->data[index + 2U] == '\n') {
                index += 3U;
                continue;
            }
        }

        if (!minipp_string_append_char(output, input->data[index])) {
            minipp_string_destroy(output);
            return false;
        }
        ++index;
    }
    return true;
}


bool minipp_render_gcc_p_output(const MiniPpString *input,
                                MiniPpString *output) {
    size_t index = 0U;
    size_t leading_spaces = 0U;
    bool line_start = true;
    bool pending_space = false;

    minipp_string_init(output);

    while (index < input->size) {
        char value = input->data[index];

        if (value == '\n') {
            leading_spaces = 0U;
            pending_space = false;
            if (!minipp_string_append_char(output, '\n')) {
                goto oom;
            }
            line_start = true;
            ++index;
            continue;
        }

        if (value == ' ' || value == '\t' ||
            value == '\v' || value == '\f') {
            if (line_start) {
                ++leading_spaces;
            } else {
                pending_space = true;
            }
            ++index;
            continue;
        }

        while (line_start && leading_spaces != 0U) {
            if (!minipp_string_append_char(output, ' ')) {
                goto oom;
            }
            --leading_spaces;
        }
        line_start = false;

        if (pending_space) {
            if (!minipp_string_append_char(output, ' ')) {
                goto oom;
            }
            pending_space = false;
        }

        if (value == '"' || value == '\'') {
            char quote = value;

            if (!minipp_string_append_char(output, value)) {
                goto oom;
            }
            ++index;
            while (index < input->size) {
                value = input->data[index];
                if (!minipp_string_append_char(output, value)) {
                    goto oom;
                }
                ++index;
                if (value == '\\' && index < input->size) {
                    if (!minipp_string_append_char(output,
                                                   input->data[index])) {
                        goto oom;
                    }
                    ++index;
                    continue;
                }
                if (value == quote) {
                    break;
                }
            }
            continue;
        }

        if (!minipp_string_append_char(output, value)) {
            goto oom;
        }
        ++index;
    }

    return true;

oom:
    minipp_string_destroy(output);
    return false;
}
