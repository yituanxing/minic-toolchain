#!/usr/bin/env python3
from pathlib import Path

p = Path("src/frontend/parser_internal.h")
text = p.read_text()
anchor = """    MinicFunctionId *perf_function_name_slots;
    size_t perf_function_name_slot_capacity;
    size_t perf_function_name_indexed_count;
} MinicParser;
"""
new = """    MinicFunctionId *perf_function_name_slots;
    size_t perf_function_name_slot_capacity;
    size_t perf_function_name_indexed_count;

    MinicGlobalObjectId *perf_global_name_slots;
    size_t perf_global_name_slot_capacity;
    size_t perf_global_name_indexed_count;
} MinicParser;
"""
if text.count(anchor) != 1:
    raise SystemExit(f"global hash field anchor count={text.count(anchor)}")
p.write_text(text.replace(anchor,new,1))

p = Path("src/frontend/parser_global.c")
text = p.read_text()
anchor = "MinicGlobalObjectId minic_parser_find_global_object_entity(const MinicParser *parser,\n"
if text.count(anchor) != 1:
    raise SystemExit(f"global lookup anchor count={text.count(anchor)}")

helpers = r'''
static size_t minic_parser_global_name_hash(const char *name, size_t length) {
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    if (name == NULL) {
        return 0U;
    }
    for (index = 0U; index < length; ++index) {
        hash ^= (unsigned char)name[index];
        hash *= UINT64_C(1099511628211);
    }
    return (size_t)hash;
}

static bool minic_parser_global_name_slot_insert(
    MinicParser *parser,
    MinicGlobalObjectId *slots,
    size_t capacity,
    MinicGlobalObjectId object_id) {
    const MinicGlobalObject *object;
    size_t mask;
    size_t slot_index;
    size_t probes;

    if (parser == NULL || parser->program == NULL || slots == NULL ||
        capacity == 0U || (capacity & (capacity - 1U)) != 0U) {
        return false;
    }
    object = minic_c0_program_global_object(parser->program, object_id);
    if (object == NULL || object->name == NULL || object->name_length == 0U) {
        return true;
    }
    mask = capacity - 1U;
    slot_index = minic_parser_global_name_hash(object->name, object->name_length) & mask;
    for (probes = 0U; probes < capacity; ++probes) {
        MinicGlobalObjectId existing_id = slots[slot_index];

        if (existing_id == MINIC_GLOBAL_OBJECT_INVALID) {
            slots[slot_index] = object_id;
            return true;
        }
        {
            const MinicGlobalObject *existing =
                minic_c0_program_global_object(parser->program, existing_id);
            if (existing != NULL &&
                existing->name_length == object->name_length &&
                memcmp(existing->name, object->name, object->name_length) == 0) {
                slots[slot_index] = object_id;
                return true;
            }
        }
        slot_index = (slot_index + 1U) & mask;
    }
    return false;
}

static bool minic_parser_global_name_index_rebuild(
    MinicParser *parser,
    size_t required_count) {
    MinicGlobalObjectId *slots;
    size_t capacity = 16U;
    size_t index;

    if (parser == NULL || parser->program == NULL) {
        return false;
    }
    while (capacity < required_count * 2U) {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / sizeof(*slots)) {
        return false;
    }
    slots = (MinicGlobalObjectId *)malloc(capacity * sizeof(*slots));
    if (slots == NULL) {
        return false;
    }
    for (index = 0U; index < capacity; ++index) {
        slots[index] = MINIC_GLOBAL_OBJECT_INVALID;
    }
    for (index = 0U; index < required_count; ++index) {
        if (!minic_parser_global_name_slot_insert(
                parser, slots, capacity, (MinicGlobalObjectId)index)) {
            free(slots);
            return false;
        }
    }
    free(parser->perf_global_name_slots);
    parser->perf_global_name_slots = slots;
    parser->perf_global_name_slot_capacity = capacity;
    parser->perf_global_name_indexed_count = required_count;
    return true;
}

static bool minic_parser_global_name_index_sync(MinicParser *parser) {
    size_t required_count;
    size_t index;

    if (parser == NULL || parser->program == NULL) {
        return false;
    }
    required_count = parser->program->global_object_count;
    if (required_count < parser->perf_global_name_indexed_count) {
        return minic_parser_global_name_index_rebuild(parser, required_count);
    }
    if (parser->perf_global_name_slots == NULL ||
        parser->perf_global_name_slot_capacity == 0U ||
        required_count > parser->perf_global_name_slot_capacity / 2U) {
        return minic_parser_global_name_index_rebuild(parser, required_count);
    }
    for (index = parser->perf_global_name_indexed_count;
         index < required_count;
         ++index) {
        if (!minic_parser_global_name_slot_insert(
                parser,
                parser->perf_global_name_slots,
                parser->perf_global_name_slot_capacity,
                (MinicGlobalObjectId)index)) {
            return false;
        }
    }
    parser->perf_global_name_indexed_count = required_count;
    return true;
}

'''
text=text.replace(anchor,helpers+anchor,1)

start=text.find(anchor)
end=text.find("\n}\n\nMinicGlobalObjectId minic_parser_find_global_object(",start)
if start < 0 or end < 0:
    raise SystemExit("cannot bound global-object lookup")
replacement=r'''MinicGlobalObjectId minic_parser_find_global_object_entity(
    const MinicParser *parser_const,
    MinicSourceSpan name_span) {
    MinicParser *parser = (MinicParser *)parser_const;
    size_t name_length;
    size_t mask;
    size_t slot_index;
    size_t probes;

    if (parser == NULL || parser->program == NULL) {
        return MINIC_GLOBAL_OBJECT_INVALID;
    }
    parser->perf_find_global_calls += 1U;
    if (!minic_parser_global_name_index_sync(parser) ||
        parser->perf_global_name_slot_capacity == 0U) {
        return MINIC_GLOBAL_OBJECT_INVALID;
    }
    name_length = minic_parser_span_length(name_span);
    mask = parser->perf_global_name_slot_capacity - 1U;
    slot_index =
        minic_parser_global_name_hash(
            parser->source + name_span.begin.offset, name_length) & mask;
    for (probes = 0U;
         probes < parser->perf_global_name_slot_capacity;
         ++probes) {
        MinicGlobalObjectId object_id =
            parser->perf_global_name_slots[slot_index];
        const MinicGlobalObject *object;

        parser->perf_find_global_steps += 1U;
        if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
            return MINIC_GLOBAL_OBJECT_INVALID;
        }
        object = minic_c0_program_global_object(parser->program, object_id);
        if (object != NULL && object->name_length == name_length &&
            memcmp(object->name,
                   parser->source + name_span.begin.offset,
                   name_length) == 0) {
            return object_id;
        }
        slot_index = (slot_index + 1U) & mask;
    }
    return MINIC_GLOBAL_OBJECT_INVALID;
}
'''
text=text[:start]+replacement+text[end+2:]
p.write_text(text)

p = Path("src/frontend/parser_function.c")
text = p.read_text()
anchor = """    free(parser.perf_function_name_slots);
    parser.perf_function_name_slots = NULL;
    parser.perf_function_name_slot_capacity = 0U;
    parser.perf_function_name_indexed_count = 0U;
    minic_parser_destroy_scopes(&parser);
"""
new = """    free(parser.perf_function_name_slots);
    parser.perf_function_name_slots = NULL;
    parser.perf_function_name_slot_capacity = 0U;
    parser.perf_function_name_indexed_count = 0U;
    free(parser.perf_global_name_slots);
    parser.perf_global_name_slots = NULL;
    parser.perf_global_name_slot_capacity = 0U;
    parser.perf_global_name_indexed_count = 0U;
    minic_parser_destroy_scopes(&parser);
"""
if text.count(anchor) != 1:
    raise SystemExit(f"global hash cleanup anchor count={text.count(anchor)}")
p.write_text(text.replace(anchor,new,1))

print("MINIC_PERF_PARSER_GLOBAL_HASH_V0=APPLIED")
