#ifndef MINIC_FRONTEND_PARSER_NAME_INDEX_H
#define MINIC_FRONTEND_PARSER_NAME_INDEX_H

#include "frontend/parser_internal.h"
#include <stdlib.h>
#include <string.h>

/* Optional file-scope name caches; the AST still owns declarations.
 * Function/global lookup keeps the earliest ID. Typedef lookup also skips
 * block aliases and falls back on inconsistent slots. Enum shadowing is
 * newest-first, so it deliberately remains a separate semantic owner.
 * All IDs here are size_t with SIZE_MAX as the empty sentinel. */
typedef enum MinicParserNameKind {
    MINIC_PARSER_NAME_FUNCTION,
    MINIC_PARSER_NAME_GLOBAL,
    MINIC_PARSER_NAME_TYPEDEF
} MinicParserNameKind;

typedef struct MinicParserName {
    const char *text;
    size_t length;
    bool visible;
    bool valid;
} MinicParserName;

static inline MinicParserName minic_parser_index_name(
    const MinicC0Program *program, MinicParserNameKind kind, size_t id) {
    MinicParserName name = {0};
    if (kind == MINIC_PARSER_NAME_FUNCTION) {
        const MinicFunction *fn = minic_c0_program_function(program, id);
        if (fn != NULL) {
            name = (MinicParserName){fn->name, fn->name_length, true, true};
        }
    } else if (kind == MINIC_PARSER_NAME_GLOBAL) {
        const MinicGlobalObject *obj = minic_c0_program_global_object(program, id);
        if (obj != NULL) {
            name = (MinicParserName){obj->name, obj->name_length, true, true};
        }
    } else {
        const MinicTypeAlias *alias = minic_c0_program_type_alias(program, id);
        if (alias != NULL) {
            name = (MinicParserName){alias->name, alias->name_length,
                                    !alias->is_block_scope, true};
        }
    }
    return name;
}

static inline size_t minic_parser_index_hash(
    MinicParserNameKind kind, const char *name, size_t length) {
    /* Keep the original seeds: changing probe order is outside this cleanup. */
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    if (kind != MINIC_PARSER_NAME_TYPEDEF)
        return minic_parser_symbol_name_hash(name, length);
    for (i = 0U; i < length; ++i) {
        hash ^= (unsigned char)name[i];
        hash *= UINT64_C(1099511628211);
    }
    return (size_t)hash;
}

static inline bool minic_parser_name_index_insert(
    const MinicC0Program *program, MinicParserNameKind kind,
    size_t *slots, size_t capacity, size_t id) {
    MinicParserName name = minic_parser_index_name(program, kind, id);
    size_t slot, probes, mask = capacity - 1U;
    if (!name.valid) return kind != MINIC_PARSER_NAME_TYPEDEF;
    if (!name.visible) return true;
    if (kind != MINIC_PARSER_NAME_TYPEDEF &&
        (name.text == NULL || name.length == 0U)) return true;
    slot = minic_parser_index_hash(kind, name.text, name.length) & mask;
    for (probes = 0U; probes < capacity; ++probes) {
        size_t prior_id = slots[slot];
        MinicParserName prior;
        if (prior_id == SIZE_MAX) {
            slots[slot] = id;
            return true;
        }
        prior = minic_parser_index_name(program, kind, prior_id);
        if (!prior.valid && kind == MINIC_PARSER_NAME_TYPEDEF) return false;
        if (prior.valid && prior.length == name.length &&
            memcmp(prior.text, name.text, name.length) == 0) {
            if (kind != MINIC_PARSER_NAME_TYPEDEF && id < prior_id)
                slots[slot] = id;
            return true;
        }
        slot = (slot + 1U) & mask;
    }
    return false;
}

/* Commit a replacement table only after every insertion has succeeded.
 * On speculative Parser copies callers must use the canonical scan instead. */
static inline bool minic_parser_name_index_sync(
    const MinicC0Program *program, MinicParserNameKind kind,
    size_t count, size_t **slots, size_t *capacity, size_t *indexed,
    size_t *rebuilds) {
    size_t i;
    if (*indexed > count || *slots == NULL || *capacity == 0U ||
        count > *capacity / 2U) {
        size_t cap = 16U;
        size_t *fresh;
        if (count > SIZE_MAX / 2U) return false;
        while (cap < count * 2U) {
            if (cap > SIZE_MAX / 2U) return false;
            cap *= 2U;
        }
        if (cap > SIZE_MAX / sizeof(*fresh)) return false;
        fresh = (size_t *)malloc(cap * sizeof(*fresh));
        if (fresh == NULL) return false;
        for (i = 0U; i < cap; ++i) fresh[i] = SIZE_MAX;
        for (i = 0U; i < count; ++i) {
            if (!minic_parser_name_index_insert(program, kind, fresh, cap, i)) {
                free(fresh);
                return false;
            }
        }
        free(*slots);
        *slots = fresh;
        *capacity = cap;
        if (rebuilds != NULL) *rebuilds += 1U;
    } else {
        for (i = *indexed; i < count; ++i) {
            if (!minic_parser_name_index_insert(program, kind, *slots, *capacity, i))
                return false;
        }
    }
    *indexed = count;
    return true;
}

static inline size_t minic_parser_name_index_lookup(
    const MinicC0Program *program, MinicParserNameKind kind,
    const size_t *slots, size_t capacity, const char *name, size_t length,
    size_t *steps, bool *consistent) {
    size_t mask = capacity - 1U;
    size_t slot = minic_parser_index_hash(kind, name, length) & mask;
    size_t probes;
    *consistent = false;
    for (probes = 0U; probes < capacity; ++probes) {
        size_t id = slots[slot];
        MinicParserName prior;
        *steps += 1U;
        if (id == SIZE_MAX) {
            *consistent = true;
            return SIZE_MAX;
        }
        prior = minic_parser_index_name(program, kind, id);
        if (!prior.valid && kind == MINIC_PARSER_NAME_TYPEDEF) break;
        if (prior.valid && prior.visible && prior.length == length &&
            memcmp(prior.text, name, length) == 0) {
            *consistent = true;
            return id;
        }
        slot = (slot + 1U) & mask;
    }
    return SIZE_MAX;
}

static inline size_t minic_parser_name_linear_lookup(
    const MinicC0Program *program, MinicParserNameKind kind,
    size_t count, const char *name, size_t length, size_t *steps) {
    size_t i;
    for (i = 0U; i < count; ++i) {
        MinicParserName prior = minic_parser_index_name(program, kind, i);
        *steps += 1U;
        if (prior.valid && prior.visible && prior.length == length &&
            memcmp(prior.text, name, length) == 0) return i;
    }
    return SIZE_MAX;
}

#endif
