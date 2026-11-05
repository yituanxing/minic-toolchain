#include "minias_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal allocation provider so this test exercises the production
 * decoder directly, independently of the complete assembler link graph. */
bool minias_grow_array(void **items, size_t *capacity,
                       size_t item_size, size_t needed) {
    size_t next = *capacity != 0U ? *capacity : 8U;
    void *grown;
    if (needed <= *capacity) return true;
    while (next < needed) {
        if (next > SIZE_MAX / 2U) { next = needed; break; }
        next *= 2U;
    }
    if (item_size == 0U || next > SIZE_MAX / item_size) return false;
    grown = realloc(*items, next * item_size);
    if (grown == NULL) return false;
    *items = grown;
    *capacity = next;
    return true;
}

static int check_valid(const char *source, bool terminate,
                       const unsigned char *expected, size_t size) {
    unsigned char *data = NULL;
    size_t actual_size = SIZE_MAX;
    int valid = minias_decode_string_literals(source, terminate,
                                               &data, &actual_size);
    int equal = valid && actual_size == size &&
                (size == 0U || (data != NULL &&
                 memcmp(data, expected, size) == 0));
    free(data);
    if (!equal) fprintf(stderr, "valid literal mismatch: %s\n", source);
    return equal;
}

static int check_invalid(const char *source) {
    unsigned char *data = NULL;
    size_t size = SIZE_MAX;
    int valid = minias_decode_string_literals(source, true, &data, &size);
    int clean = !valid && data == NULL && size == 0U;
    free(data);
    if (!clean) fprintf(stderr, "invalid literal retained data: %s\n", source);
    return clean;
}

int main(void) {
    const unsigned char pairs[] = {'o','n','e',0,'t','w','o',0};
    const unsigned char escaped[] = {'A','\n','B','A'};
    const unsigned char empty[] = {0};
    const unsigned char single[] = {'x'};
    const char *const bad[] = {
        "bad", "\"ok\",broken", "\"bad\\q\"",
        "\"bad\\x\"", "\"unterminated",
        "\"x\",", "\"x\" trailing", "\"bad\\"
    };
    size_t i;
    if (!check_valid("\"one\",\"two\"", true, pairs, sizeof(pairs)) ||
        !check_valid("\"A\\n\\x42\\101\"", false, escaped, sizeof(escaped)) ||
        !check_valid("\"\"", true, empty, sizeof(empty)) ||
        !check_valid("\"x\"", false, single, sizeof(single)) ||
        !check_valid("\"\"", false, empty, 0U)) return 1;
    for (i = 0U; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        if (!check_invalid(bad[i])) return 1;
    }
    if (minias_decode_string_literals("\"valid\"", false, NULL, NULL))
        return 1;
    puts("MINIAS_STRING_DECODE_CLEANUP=PASS valid=5 invalid=8");
    return 0;
}
