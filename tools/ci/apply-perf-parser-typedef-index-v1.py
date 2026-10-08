#!/usr/bin/env python3
"""P05: first-match global typedef hash index, preserving local shadowing.

Local scope bindings retain their original reverse scan. Global typedef names
are indexed incrementally by TypeAliasId, in declaration order. Allocation or
consistency failures fall back to the existing linear search.
"""
from pathlib import Path

def patch(path, old, new):
    p = Path(path)
    src = p.read_text()
    n = src.count(old)
    if n != 1:
        raise SystemExit(f"{path}: P05 anchor count={n}, expected one: {old[:100]!r}")
    p.write_text(src.replace(old, new, 1))

patch("src/frontend/parser_internal.h",
      "    size_t perf_enum_name_indexed_count;\n} MinicParser;\n",
      """    size_t perf_enum_name_indexed_count;

    MinicTypeAliasId *perf_typedef_name_slots;
    size_t perf_typedef_name_capacity;
    size_t perf_typedef_name_indexed_count;
    size_t perf_typedef_queries;
    size_t perf_typedef_probes;
    size_t perf_typedef_fallbacks;
    size_t perf_typedef_rebuilds;
} MinicParser;
""")
patch("src/frontend/parser_typedef.c", "#include <limits.h>\n",
      "#include <limits.h>\n#include <stdint.h>\n")
helpers = r'''
static size_t minic_parser_typedef_name_hash(const char *name, size_t length) {
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0U; i < length; ++i) {
        hash ^= (unsigned char)name[i];
        hash *= UINT64_C(1099511628211);
    }
    return (size_t)hash;
}

static bool minic_parser_typedef_slot_insert(
    const MinicC0Program *program, MinicTypeAliasId *slots,
    size_t capacity, MinicTypeAliasId alias_id) {
    const MinicTypeAlias *alias = minic_c0_program_type_alias(program, alias_id);
    size_t slot, n;
    if (alias == NULL || slots == NULL || capacity == 0U) {
        return false;
    }
    if (alias->is_block_scope) {
        return true;
    }
    slot = minic_parser_typedef_name_hash(alias->name, alias->name_length) &
           (capacity - 1U);
    for (n = 0U; n < capacity; ++n) {
        MinicTypeAliasId prior_id = slots[slot];
        if (prior_id == MINIC_TYPE_ALIAS_INVALID) {
            slots[slot] = alias_id;
            return true;
        }
        if (prior_id >= program->type_alias_count) {
            return false;
        }
        {
            const MinicTypeAlias *prior = &program->type_aliases[prior_id];
            /* Retain earliest declared global alias on equal names. */
            if (prior->name_length == alias->name_length &&
                memcmp(prior->name, alias->name, alias->name_length) == 0) {
                return true;
            }
        }
        slot = (slot + 1U) & (capacity - 1U);
    }
    return false;
}

static bool minic_parser_typedef_index_rebuild(MinicParser *parser) {
    const MinicC0Program *program = parser->program;
    MinicTypeAliasId *slots;
    size_t capacity = 16U, index;
    if (program == NULL || program->type_alias_count > SIZE_MAX / 2U) {
        return false;
    }
    while (capacity < program->type_alias_count * 2U) {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / sizeof(*slots)) {
        return false;
    }
    slots = (MinicTypeAliasId *)malloc(capacity * sizeof(*slots));
    if (slots == NULL) {
        return false;
    }
    for (index = 0U; index < capacity; ++index) {
        slots[index] = MINIC_TYPE_ALIAS_INVALID;
    }
    for (index = 0U; index < program->type_alias_count; ++index) {
        if (!minic_parser_typedef_slot_insert(program, slots, capacity, index)) {
            free(slots);
            return false;
        }
    }
    free(parser->perf_typedef_name_slots);
    parser->perf_typedef_name_slots = slots;
    parser->perf_typedef_name_capacity = capacity;
    parser->perf_typedef_name_indexed_count = program->type_alias_count;
    parser->perf_typedef_rebuilds += 1U;
    return true;
}

static bool minic_parser_typedef_index_sync(MinicParser *parser) {
    size_t index, count;
    if (parser == NULL || parser->program == NULL) {
        return false;
    }
    count = parser->program->type_alias_count;
    if (parser->perf_typedef_name_indexed_count > count ||
        parser->perf_typedef_name_slots == NULL ||
        count > parser->perf_typedef_name_capacity / 2U) {
        return minic_parser_typedef_index_rebuild(parser);
    }
    for (index = parser->perf_typedef_name_indexed_count;
         index < count; ++index) {
        if (!minic_parser_typedef_slot_insert(
                parser->program, parser->perf_typedef_name_slots,
                parser->perf_typedef_name_capacity, index)) {
            return false;
        }
    }
    parser->perf_typedef_name_indexed_count = count;
    return true;
}

static MinicTypeAliasId minic_parser_typedef_index_lookup(
    MinicParser *parser, MinicSourceSpan span, bool *consistent) {
    const char *name = parser->source + span.begin.offset;
    size_t length = minic_parser_span_length(span);
    size_t capacity = parser->perf_typedef_name_capacity;
    size_t slot = minic_parser_typedef_name_hash(name, length) & (capacity - 1U);
    size_t n;
    *consistent = false;
    for (n = 0U; n < capacity; ++n) {
        MinicTypeAliasId id = parser->perf_typedef_name_slots[slot];
        parser->perf_typedef_probes += 1U;
        if (id == MINIC_TYPE_ALIAS_INVALID) {
            *consistent = true;
            return MINIC_TYPE_ALIAS_INVALID;
        }
        if (id >= parser->program->type_alias_count) {
            break;
        }
        {
            const MinicTypeAlias *alias = &parser->program->type_aliases[id];
            if (!alias->is_block_scope && alias->name_length == length &&
                memcmp(alias->name, name, length) == 0) {
                *consistent = true;
                return id;
            }
        }
        slot = (slot + 1U) & (capacity - 1U);
    }
    /* Inconsistent table: let the caller do the original linear scan. */
    return MINIC_TYPE_ALIAS_INVALID;
}
'''
patch("src/frontend/parser_typedef.c",
      "MinicTypeAliasId minic_parser_find_type_alias(const MinicParser *parser,\n",
      helpers + "\nMinicTypeAliasId minic_parser_find_type_alias(const MinicParser *parser,\n")
# A confirmed table miss is NOT equivalent to an index-build failure.
patch("src/frontend/parser_typedef.c",
      """    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->type_alias_count; ++index) {""",
      """    ((MinicParser *)parser)->perf_typedef_queries += 1U;
    if (minic_parser_typedef_index_sync((MinicParser *)parser)) {
        bool consistent = false;
        MinicTypeAliasId id = minic_parser_typedef_index_lookup(
            (MinicParser *)parser, name_span, &consistent);
        if (consistent) {
            return id;
        }
    }
    ((MinicParser *)parser)->perf_typedef_fallbacks += 1U;
    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->type_alias_count; ++index) {""")
patch("src/frontend/parser_core.c",
      """void minic_parser_destroy_scopes(MinicParser *parser) {
    free(parser->local_labels);""",
      """void minic_parser_destroy_scopes(MinicParser *parser) {
    if (getenv("MINIC_TYPEDEF_INDEX_PERF") != NULL) {
        (void)fprintf(stderr,
                      "MINIC_TYPEDEF_INDEX queries=%zu probes=%zu fallback=%zu "
                      "rebuilds=%zu aliases=%zu\\n",
                      parser->perf_typedef_queries, parser->perf_typedef_probes,
                      parser->perf_typedef_fallbacks, parser->perf_typedef_rebuilds,
                      parser->program == NULL ? 0U : parser->program->type_alias_count);
    }
    free(parser->perf_typedef_name_slots);
    parser->perf_typedef_name_slots = NULL;
    free(parser->local_labels);""")
print("MINIC_PERF_PARSER_TYPEDEF_HASH_V1=APPLIED")
