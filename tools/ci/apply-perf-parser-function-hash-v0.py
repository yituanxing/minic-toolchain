#!/usr/bin/env python3
from pathlib import Path

# Parser-lifetime hash index for function-name lookup. Functions are added
# monotonically while parsing.  Incremental insertion preserves the old
# reverse-scan rule: the newest function id replaces any older id with the same
# spelling.

p = Path("src/frontend/parser_internal.h")
text = p.read_text()
anchor = """    size_t perf_builtin_mem_calls;
    size_t perf_builtin_mem_steps;
} MinicParser;
"""
new = """    size_t perf_builtin_mem_calls;
    size_t perf_builtin_mem_steps;

    MinicFunctionId *perf_function_name_slots;
    size_t perf_function_name_slot_capacity;
    size_t perf_function_name_indexed_count;
} MinicParser;
"""
if text.count(anchor) != 1:
    raise SystemExit(f"parser hash fields anchor count={text.count(anchor)}")
p.write_text(text.replace(anchor,new,1))

p = Path("src/frontend/parser_core.c")
text = p.read_text()
anchor = "MinicFunctionId minic_parser_find_function(const MinicParser *parser, MinicSourceSpan name_span) {\n"
if text.count(anchor) != 1:
    raise SystemExit(f"find-function anchor count={text.count(anchor)}")

helpers = r'''
static size_t minic_parser_function_name_hash(const char *name, size_t length) {
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

static bool minic_parser_function_name_slot_insert(
    MinicParser *parser,
    MinicFunctionId *slots,
    size_t capacity,
    MinicFunctionId function_id) {
    const MinicFunction *function;
    size_t mask;
    size_t slot_index;
    size_t probes;

    if (parser == NULL || parser->program == NULL || slots == NULL ||
        capacity == 0U || (capacity & (capacity - 1U)) != 0U) {
        return false;
    }
    function = minic_c0_program_function(parser->program, function_id);
    if (function == NULL || function->name == NULL || function->name_length == 0U) {
        return true;
    }
    mask = capacity - 1U;
    slot_index = minic_parser_function_name_hash(function->name, function->name_length) & mask;
    for (probes = 0U; probes < capacity; ++probes) {
        MinicFunctionId existing_id = slots[slot_index];

        if (existing_id == MINIC_FUNCTION_INVALID) {
            slots[slot_index] = function_id;
            return true;
        }
        {
            const MinicFunction *existing =
                minic_c0_program_function(parser->program, existing_id);
            if (existing != NULL &&
                existing->name_length == function->name_length &&
                memcmp(existing->name, function->name, function->name_length) == 0) {
                /* Ascending incremental insertion means a later id must win,
                   matching the previous reverse linear scan. */
                slots[slot_index] = function_id;
                return true;
            }
        }
        slot_index = (slot_index + 1U) & mask;
    }
    return false;
}

static bool minic_parser_function_name_index_rebuild(
    MinicParser *parser,
    size_t required_count) {
    MinicFunctionId *slots;
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
    slots = (MinicFunctionId *)malloc(capacity * sizeof(*slots));
    if (slots == NULL) {
        return false;
    }
    for (index = 0U; index < capacity; ++index) {
        slots[index] = MINIC_FUNCTION_INVALID;
    }
    for (index = 0U; index < required_count; ++index) {
        if (!minic_parser_function_name_slot_insert(
                parser, slots, capacity, (MinicFunctionId)index)) {
            free(slots);
            return false;
        }
    }
    free(parser->perf_function_name_slots);
    parser->perf_function_name_slots = slots;
    parser->perf_function_name_slot_capacity = capacity;
    parser->perf_function_name_indexed_count = required_count;
    return true;
}

static bool minic_parser_function_name_index_sync(MinicParser *parser) {
    size_t required_count;
    size_t index;

    if (parser == NULL || parser->program == NULL) {
        return false;
    }
    required_count = parser->program->function_count;
    if (required_count < parser->perf_function_name_indexed_count) {
        return minic_parser_function_name_index_rebuild(parser, required_count);
    }
    if (parser->perf_function_name_slots == NULL ||
        parser->perf_function_name_slot_capacity == 0U ||
        required_count > parser->perf_function_name_slot_capacity / 2U) {
        return minic_parser_function_name_index_rebuild(parser, required_count);
    }
    for (index = parser->perf_function_name_indexed_count;
         index < required_count;
         ++index) {
        if (!minic_parser_function_name_slot_insert(
                parser,
                parser->perf_function_name_slots,
                parser->perf_function_name_slot_capacity,
                (MinicFunctionId)index)) {
            return false;
        }
    }
    parser->perf_function_name_indexed_count = required_count;
    return true;
}

'''
text=text.replace(anchor,helpers+anchor,1)

start=text.find(anchor)
end=text.find("\n}\n\nMinicRecordId minic_parser_find_record(",start)
if start < 0 or end < 0:
    raise SystemExit("cannot bound find-function implementation")
replacement=r'''MinicFunctionId minic_parser_find_function(const MinicParser *parser_const,
                                                 MinicSourceSpan name_span) {
    MinicParser *parser = (MinicParser *)parser_const;
    size_t name_length;
    size_t mask;
    size_t slot_index;
    size_t probes;

    if (parser == NULL || parser->program == NULL) {
        return MINIC_FUNCTION_INVALID;
    }
    parser->perf_find_function_calls += 1U;
    if (!minic_parser_function_name_index_sync(parser) ||
        parser->perf_function_name_slot_capacity == 0U) {
        return MINIC_FUNCTION_INVALID;
    }
    name_length = minic_parser_span_length(name_span);
    mask = parser->perf_function_name_slot_capacity - 1U;
    slot_index =
        minic_parser_function_name_hash(
            parser->source + name_span.begin.offset, name_length) & mask;
    for (probes = 0U;
         probes < parser->perf_function_name_slot_capacity;
         ++probes) {
        MinicFunctionId function_id =
            parser->perf_function_name_slots[slot_index];
        const MinicFunction *function;

        parser->perf_find_function_steps += 1U;
        if (function_id == MINIC_FUNCTION_INVALID) {
            return MINIC_FUNCTION_INVALID;
        }
        function = minic_c0_program_function(parser->program, function_id);
        if (function != NULL && function->name_length == name_length &&
            memcmp(function->name,
                   parser->source + name_span.begin.offset,
                   name_length) == 0) {
            return function_id;
        }
        slot_index = (slot_index + 1U) & mask;
    }
    return MINIC_FUNCTION_INVALID;
}
'''
text=text[:start]+replacement+text[end+2:]
p.write_text(text)

# Free parser-owned lookup storage at the same parser lifetime boundary as
# local scope storage.
p = Path("src/frontend/parser_function.c")
text = p.read_text()
anchor = """    minic_parser_destroy_scopes(&parser);
    minic_parser_destroy_enum_constants(&parser);
    return success;
"""
new = """    free(parser.perf_function_name_slots);
    parser.perf_function_name_slots = NULL;
    parser.perf_function_name_slot_capacity = 0U;
    parser.perf_function_name_indexed_count = 0U;
    minic_parser_destroy_scopes(&parser);
    minic_parser_destroy_enum_constants(&parser);
    return success;
"""
if text.count(anchor) != 1:
    raise SystemExit(f"parser hash cleanup anchor count={text.count(anchor)}")
p.write_text(text.replace(anchor,new,1))

print("MINIC_PERF_PARSER_FUNCTION_HASH_V0=APPLIED")
