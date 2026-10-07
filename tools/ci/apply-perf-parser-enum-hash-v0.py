#!/usr/bin/env python3
from pathlib import Path

p = Path("src/frontend/parser_internal.h")
text = p.read_text()
anchor = """    MinicGlobalObjectId *perf_global_name_slots;
    size_t perf_global_name_slot_capacity;
    size_t perf_global_name_indexed_count;
} MinicParser;
"""
new = """    MinicGlobalObjectId *perf_global_name_slots;
    size_t perf_global_name_slot_capacity;
    size_t perf_global_name_indexed_count;

    size_t *perf_enum_name_slots;
    size_t perf_enum_name_slot_capacity;
    size_t perf_enum_name_indexed_count;
} MinicParser;
"""
if text.count(anchor) != 1:
    raise SystemExit(f"enum hash field anchor count={text.count(anchor)}")
p.write_text(text.replace(anchor,new,1))

p = Path("src/frontend/parser_enum.c")
text = p.read_text()
anchor = "MinicEnumeratorId minic_parser_find_enum_constant(const MinicParser *parser,\n"
if text.count(anchor) != 1:
    raise SystemExit(f"enum lookup anchor count={text.count(anchor)}")

helpers = r'''
static size_t minic_parser_enum_name_hash(const MinicParser *parser,
                                          MinicSourceSpan span) {
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t length;
    size_t index;
    const char *name;

    if (parser == NULL || parser->source == NULL) {
        return 0U;
    }
    length = minic_parser_span_length(span);
    name = parser->source + span.begin.offset;
    for (index = 0U; index < length; ++index) {
        hash ^= (unsigned char)name[index];
        hash *= UINT64_C(1099511628211);
    }
    return (size_t)hash;
}

static bool minic_parser_enum_global_slot_insert(
    MinicParser *parser,
    size_t *slots,
    size_t capacity,
    size_t constant_index) {
    const MinicParserEnumConstant *constant;
    size_t mask;
    size_t slot_index;
    size_t probes;

    if (parser == NULL || slots == NULL || constant_index >= parser->enum_constant_count ||
        capacity == 0U || (capacity & (capacity - 1U)) != 0U) {
        return false;
    }
    constant = &parser->enum_constants[constant_index];
    mask = capacity - 1U;
    slot_index = minic_parser_enum_name_hash(parser, constant->name_span) & mask;
    for (probes = 0U; probes < capacity; ++probes) {
        size_t existing_index = slots[slot_index];

        if (existing_index == SIZE_MAX) {
            slots[slot_index] = constant_index;
            return true;
        }
        if (existing_index < parser->enum_constant_count &&
            minic_parser_span_equals(
                parser,
                parser->enum_constants[existing_index].name_span,
                constant->name_span)) {
            slots[slot_index] = constant_index;
            return true;
        }
        slot_index = (slot_index + 1U) & mask;
    }
    return false;
}

static bool minic_parser_enum_global_index_rebuild(
    MinicParser *parser,
    size_t global_count) {
    size_t *slots;
    size_t capacity = 16U;
    size_t index;

    if (parser == NULL || global_count > parser->enum_constant_count) {
        return false;
    }
    while (capacity < global_count * 2U) {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / sizeof(*slots)) {
        return false;
    }
    slots = (size_t *)malloc(capacity * sizeof(*slots));
    if (slots == NULL) {
        return false;
    }
    for (index = 0U; index < capacity; ++index) {
        slots[index] = SIZE_MAX;
    }
    for (index = 0U; index < global_count; ++index) {
        if (!minic_parser_enum_global_slot_insert(
                parser, slots, capacity, index)) {
            free(slots);
            return false;
        }
    }
    free(parser->perf_enum_name_slots);
    parser->perf_enum_name_slots = slots;
    parser->perf_enum_name_slot_capacity = capacity;
    parser->perf_enum_name_indexed_count = global_count;
    return true;
}

static bool minic_parser_enum_global_index_sync(
    MinicParser *parser,
    size_t global_count) {
    size_t index;

    if (parser == NULL || global_count > parser->enum_constant_count ||
        global_count < parser->perf_enum_name_indexed_count) {
        return false;
    }
    if (parser->perf_enum_name_slots == NULL ||
        parser->perf_enum_name_slot_capacity == 0U ||
        global_count > parser->perf_enum_name_slot_capacity / 2U) {
        return minic_parser_enum_global_index_rebuild(parser, global_count);
    }
    for (index = parser->perf_enum_name_indexed_count;
         index < global_count;
         ++index) {
        if (!minic_parser_enum_global_slot_insert(
                parser,
                parser->perf_enum_name_slots,
                parser->perf_enum_name_slot_capacity,
                index)) {
            return false;
        }
    }
    parser->perf_enum_name_indexed_count = global_count;
    return true;
}

'''
text=text.replace(anchor,helpers+anchor,1)

start=text.find(anchor)
end=text.find("\n}\n\nbool minic_parser_bind_enum_constant(",start)
if start < 0 or end < 0:
    raise SystemExit("cannot bound enum lookup")
replacement=r'''MinicEnumeratorId minic_parser_find_enum_constant(
    const MinicParser *parser_const,
    MinicSourceSpan name_span) {
    MinicParser *parser = (MinicParser *)parser_const;
    size_t global_count;
    size_t index;
    size_t mask;
    size_t slot_index;
    size_t probes;

    if (parser == NULL) {
        return MINIC_ENUMERATOR_INVALID;
    }
    parser->perf_find_enum_calls += 1U;

    /* Enumerators added after the outermost active scope began are local to
       that function/block lifetime and may shadow file-scope names. Keep the
       existing newest-first semantics for this usually-small suffix. */
    global_count = parser->scope_count == 0U
                       ? parser->enum_constant_count
                       : parser->scopes[0].enum_constant_begin;
    for (index = parser->enum_constant_count; index > global_count; --index) {
        const MinicParserEnumConstant *constant =
            &parser->enum_constants[index - 1U];
        parser->perf_find_enum_steps += 1U;
        if (minic_parser_span_equals(parser, name_span, constant->name_span)) {
            return constant->enumerator_id;
        }
    }

    if (global_count == 0U) {
        return MINIC_ENUMERATOR_INVALID;
    }
    if (!minic_parser_enum_global_index_sync(parser, global_count) ||
        parser->perf_enum_name_slot_capacity == 0U) {
        return MINIC_ENUMERATOR_INVALID;
    }
    mask = parser->perf_enum_name_slot_capacity - 1U;
    slot_index = minic_parser_enum_name_hash(parser, name_span) & mask;
    for (probes = 0U;
         probes < parser->perf_enum_name_slot_capacity;
         ++probes) {
        size_t constant_index = parser->perf_enum_name_slots[slot_index];

        parser->perf_find_enum_steps += 1U;
        if (constant_index == SIZE_MAX) {
            return MINIC_ENUMERATOR_INVALID;
        }
        if (constant_index < global_count &&
            minic_parser_span_equals(
                parser,
                name_span,
                parser->enum_constants[constant_index].name_span)) {
            return parser->enum_constants[constant_index].enumerator_id;
        }
        slot_index = (slot_index + 1U) & mask;
    }
    return MINIC_ENUMERATOR_INVALID;
}
'''
text=text[:start]+replacement+text[end+2:]
p.write_text(text)

p = Path("src/frontend/parser_function.c")
text = p.read_text()
anchor = """    free(parser.perf_global_name_slots);
    parser.perf_global_name_slots = NULL;
    parser.perf_global_name_slot_capacity = 0U;
    parser.perf_global_name_indexed_count = 0U;
    minic_parser_destroy_scopes(&parser);
"""
new = """    free(parser.perf_global_name_slots);
    parser.perf_global_name_slots = NULL;
    parser.perf_global_name_slot_capacity = 0U;
    parser.perf_global_name_indexed_count = 0U;
    free(parser.perf_enum_name_slots);
    parser.perf_enum_name_slots = NULL;
    parser.perf_enum_name_slot_capacity = 0U;
    parser.perf_enum_name_indexed_count = 0U;
    minic_parser_destroy_scopes(&parser);
"""
if text.count(anchor) != 1:
    raise SystemExit(f"enum hash cleanup anchor count={text.count(anchor)}")
p.write_text(text.replace(anchor,new,1))

print("MINIC_PERF_PARSER_ENUM_HASH_V0=APPLIED")
