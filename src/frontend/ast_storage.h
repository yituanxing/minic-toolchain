#ifndef MINIC_FRONTEND_AST_STORAGE_H
#define MINIC_FRONTEND_AST_STORAGE_H

/* Private allocation policy shared by the AST and global-object arenas. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline bool minic_ast_grow_array(void **data, size_t *capacity, size_t count, size_t element_size) {
    void *resized;
    size_t new_capacity;

    if (count < *capacity) {
        return true;
    }

    new_capacity = *capacity == 0U ? 16U : *capacity * 2U;
    if (new_capacity < *capacity || new_capacity > SIZE_MAX / element_size) {
        return false;
    }

    resized = realloc(*data, new_capacity * element_size);
    if (resized == NULL) {
        return false;
    }
    *data = resized;
    *capacity = new_capacity;
    return true;
}

static inline char *minic_ast_copy_name(const char *name, size_t name_length) {
    char *copy;

    if (name == NULL || name_length == SIZE_MAX) {
        return NULL;
    }
    copy = (char *)malloc(name_length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    (void)memcpy(copy, name, name_length);
    copy[name_length] = '\0';
    return copy;
}

/* Checked backing-store shape; empty arenas may have no allocation. */
static inline bool minic_ast_storage_is_valid(const void *data, size_t count,
                                               size_t capacity) {
    return count <= capacity && (count == 0U || data != NULL);
}

/* Immutable nonempty spellings agree across object and function redeclarations. */
static inline bool minic_ast_set_owned_name(char **storage, size_t *length,
                                            const char *name, size_t name_length) {
    char *copy;
    if (name == NULL || name_length == 0U || name_length == SIZE_MAX)
        return false;
    if (*storage != NULL)
        return *length == name_length && memcmp(*storage, name, name_length) == 0;
    copy = minic_ast_copy_name(name, name_length);
    if (copy == NULL) return false;
    *storage = copy;
    *length = name_length;
    return true;
}

#endif
