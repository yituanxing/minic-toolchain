#include "minias_internal.h"

#include <ctype.h>
#include <stdlib.h>

static bool append_byte(unsigned char **data,
                        size_t *size,
                        size_t *capacity,
                        unsigned int value) {
    unsigned char *next;
    size_t next_capacity;

    if (*size == *capacity) {
        next_capacity = *capacity == 0U ? 32U : *capacity * 2U;
        next = realloc(*data, next_capacity);
        if (next == NULL) {
            return false;
        }
        *data = next;
        *capacity = next_capacity;
    }
    (*data)[(*size)++] = (unsigned char)(value & 0xffU);
    return true;
}

static int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool minias_decode_string_literals(const char *text,
                                   bool nul_terminate,
                                   unsigned char **data,
                                   size_t *size) {
    const char *p = text;
    size_t capacity = 0U;

    if (data == NULL || size == NULL) {
        return false;
    }
    *data = NULL;
    *size = 0U;

    for (;;) {
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (*p != '"') {
            free(*data);
            *data = NULL;
            *size = 0U;
            return false;
        }
        ++p;
        while (*p != '\0' && *p != '"') {
            unsigned int value;

            if (*p != '\\') {
                value = (unsigned char)*p++;
            } else {
                ++p;
                if (*p == '\0') {
                    free(*data);
                    *data = NULL;
                    *size = 0U;
                    return false;
                }
                switch (*p) {
                case 'a': value = 7U; ++p; break;
                case 'b': value = 8U; ++p; break;
                case 'f': value = 12U; ++p; break;
                case 'n': value = 10U; ++p; break;
                case 'r': value = 13U; ++p; break;
                case 't': value = 9U; ++p; break;
                case 'v': value = 11U; ++p; break;
                case '\\': value = '\\'; ++p; break;
                case '"': value = '"'; ++p; break;
                case '\'': value = '\''; ++p; break;
                case '?': value = '?'; ++p; break;
                case 'x': {
                    int digit;
                    unsigned int accum = 0U;
                    bool any = false;
                    ++p;
                    while ((digit = hex_value(*p)) >= 0) {
                        accum = (accum << 4U) | (unsigned int)digit;
                        any = true;
                        ++p;
                    }
                    if (!any) {
                        free(*data);
                        *data = NULL;
                        *size = 0U;
                        return false;
                    }
                    value = accum;
                    break;
                }
                default:
                    if (*p >= '0' && *p <= '7') {
                        unsigned int accum = 0U;
                        unsigned int count = 0U;
                        while (count < 3U && *p >= '0' && *p <= '7') {
                            accum = accum * 8U + (unsigned int)(*p - '0');
                            ++p;
                            ++count;
                        }
                        value = accum;
                    } else {
                        free(*data);
                        *data = NULL;
                        *size = 0U;
                        return false;
                    }
                    break;
                }
            }
            if (!append_byte(data, size, &capacity, value)) {
                free(*data);
                *data = NULL;
                *size = 0U;
                return false;
            }
        }
        if (*p != '"') {
            free(*data);
            *data = NULL;
            *size = 0U;
            return false;
        }
        ++p;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p == '"') {
            continue;
        }
        if (*p == '\0') {
            if (nul_terminate && !append_byte(data, size, &capacity, 0U)) {
                free(*data);
                *data = NULL;
                *size = 0U;
                return false;
            }
            return true;
        }
        free(*data);
        *data = NULL;
        *size = 0U;
        return false;
    }
}
