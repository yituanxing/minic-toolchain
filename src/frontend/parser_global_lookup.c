#include "frontend/parser_internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Name resolution: global object indexing and fixed register bindings. */

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
                /* Preserve the original forward linear lookup: the
                   earliest global declaration wins on duplicate names. */
                if (object_id < existing_id) {
                    slots[slot_index] = object_id;
                }
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
    if (required_count > SIZE_MAX / 2U) {
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

/* Same first-declaration rule as the original forward scan.
 * An allocation or index-sync failure must not hide an existing global. */
static MinicGlobalObjectId minic_parser_global_linear_fallback(
    MinicParser *parser, MinicSourceSpan name_span) {
    size_t length = minic_parser_span_length(name_span);
    size_t index;
    for (index = 0U; index < parser->program->global_object_count; ++index) {
        const MinicGlobalObject *object =
            minic_c0_program_global_object(parser->program, index);
        parser->perf_find_global_steps += 1U;
        if (object != NULL && object->name_length == length &&
            memcmp(object->name, parser->source + name_span.begin.offset,
                   length) == 0) {
            return (MinicGlobalObjectId)index;
        }
    }
    return MINIC_GLOBAL_OBJECT_INVALID;
}

MinicGlobalObjectId minic_parser_find_global_object_entity(
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
#if defined(MINIC_TEST_FORCE_PARSER_HASH_FALLBACK)
    return minic_parser_global_linear_fallback(parser, name_span);
#endif
    if (parser->cache_owner != parser ||
        !minic_parser_global_name_index_sync(parser) ||
        parser->perf_global_name_slot_capacity == 0U) {
        return minic_parser_global_linear_fallback(parser, name_span);
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


MinicGlobalObjectId minic_parser_find_global_object(const MinicParser *parser,
                                                    MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    const MinicGlobalObject *object;

    object_id = minic_parser_find_scoped_global_object(parser, name_span);
    if (object_id != MINIC_GLOBAL_OBJECT_INVALID) {
        return object_id;
    }
    object_id = minic_parser_find_global_object_entity(parser, name_span);
    object = object_id == MINIC_GLOBAL_OBJECT_INVALID
                 ? NULL
                 : minic_c0_program_global_object(parser->program, object_id);
    return object != NULL && !object->is_block_scope_extern_only ? object_id
                                                                 : MINIC_GLOBAL_OBJECT_INVALID;
}

MinicFixedRegisterBindingId minic_parser_find_fixed_register_binding(const MinicParser *parser,
                                                                     MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    if (parser == NULL || parser->program == NULL) {
        return MINIC_FIXED_REGISTER_BINDING_INVALID;
    }
    ((MinicParser *)parser)->perf_find_fixed_calls += 1U;
    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->fixed_register_binding_count; ++index) {
        const MinicFixedRegisterBinding *binding;
        ((MinicParser *)parser)->perf_find_fixed_steps += 1U;

        binding = minic_c0_program_fixed_register_binding(parser->program, index);
        if (binding != NULL && !binding->is_local && binding->name_length == name_length &&
            memcmp(binding->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
    }
    return MINIC_FIXED_REGISTER_BINDING_INVALID;
}
