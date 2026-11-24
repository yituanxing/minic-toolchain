#include "frontend/parser_internal.h"
#include "frontend/parser_name_index.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Name resolution: global object indexing and fixed register bindings. */

MinicGlobalObjectId minic_parser_find_global_object_entity(const MinicParser *parser_const,
                                                  MinicSourceSpan name_span) {
    MinicParser *parser = (MinicParser *)parser_const;
    const char *name;
    size_t length;
    bool consistent;
    if (parser == NULL || parser->program == NULL) return SIZE_MAX;
    parser->perf_find_global_calls += 1U;
    name = parser->source + name_span.begin.offset;
    length = minic_parser_span_length(name_span);
#if defined(MINIC_TEST_FORCE_PARSER_HASH_FALLBACK)
    return minic_parser_name_linear_lookup(parser, MINIC_PARSER_NAME_GLOBAL,
        parser->program->global_object_count, name, length, &parser->perf_find_global_steps);
#endif
    if (parser->cache_owner != parser ||
        !minic_parser_name_index_sync(parser, MINIC_PARSER_NAME_GLOBAL,
            parser->program->global_object_count, &parser->perf_global_name_slots,
            &parser->perf_global_name_slot_capacity,
            &parser->perf_global_name_indexed_count, NULL) ||
        parser->perf_global_name_slot_capacity == 0U)
        return minic_parser_name_linear_lookup(parser, MINIC_PARSER_NAME_GLOBAL,
            parser->program->global_object_count, name, length, &parser->perf_find_global_steps);
    return minic_parser_name_index_lookup(parser, MINIC_PARSER_NAME_GLOBAL,
        parser->perf_global_name_slots, parser->perf_global_name_slot_capacity,
        name, length, &parser->perf_find_global_steps, &consistent);
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
