#!/usr/bin/env python3
"""P10: fail-safe linear fallback when perf-only parser hash indexes cannot sync.

A failed hash-table allocation or inconsistent saturated table is not proof that
an entity does not exist. Keep canonical lookup ordering and scope semantics.
"""
from pathlib import Path

def patch(path, old, new):
    p = Path(path)
    text = p.read_text()
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"P10 expected one anchor in {path}, found {n}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1))

function_fallback = r'''static MinicFunctionId minic_parser_find_function_linear_fallback(
    const MinicParser *parser, MinicSourceSpan span) {
    size_t index, length = minic_parser_span_length(span);
    for (index = parser->program->function_count; index > 0U; --index) {
        size_t id = index - 1U;
        const MinicFunction *fn = minic_c0_program_function(parser->program, id);
        if (fn != NULL && fn->name_length == length &&
            memcmp(fn->name, parser->source + span.begin.offset, length) == 0) {
            return id;
        }
    }
    return MINIC_FUNCTION_INVALID;
}

'''
patch("src/frontend/parser_core.c",
      "MinicFunctionId minic_parser_find_function(const MinicParser *parser_const,\n",
      function_fallback + "MinicFunctionId minic_parser_find_function(const MinicParser *parser_const,\n")
patch("src/frontend/parser_core.c",
      "    parser->perf_find_function_calls += 1U;\n",
      """    parser->perf_find_function_calls += 1U;
#if defined(MINIC_TEST_FORCE_PARSER_HASH_FALLBACK)
    return minic_parser_find_function_linear_fallback(parser, name_span);
#endif
""")
patch("src/frontend/parser_core.c",
      """    if (!minic_parser_function_name_index_sync(parser) ||
        parser->perf_function_name_slot_capacity == 0U) {
        return MINIC_FUNCTION_INVALID;
    }""",
      """    if (!minic_parser_function_name_index_sync(parser) ||
        parser->perf_function_name_slot_capacity == 0U) {
        return minic_parser_find_function_linear_fallback(parser, name_span);
    }""")
patch("src/frontend/parser_core.c",
      """    return MINIC_FUNCTION_INVALID;
}

MinicRecordId minic_parser_find_record(""",
      """    return minic_parser_find_function_linear_fallback(parser, name_span);
}

MinicRecordId minic_parser_find_record(""")
global_fallback = r'''static MinicGlobalObjectId minic_parser_find_global_object_linear_fallback(
    const MinicParser *parser, MinicSourceSpan span) {
    size_t index, length = minic_parser_span_length(span);
    for (index = parser->program->global_object_count; index > 0U; --index) {
        size_t id = index - 1U;
        const MinicGlobalObject *object = minic_c0_program_global_object(parser->program, id);
        if (object != NULL && object->name_length == length &&
            memcmp(object->name, parser->source + span.begin.offset, length) == 0) {
            return id;
        }
    }
    return MINIC_GLOBAL_OBJECT_INVALID;
}

'''
patch("src/frontend/parser_global.c",
      "MinicGlobalObjectId minic_parser_find_global_object_entity(\n",
      global_fallback + "MinicGlobalObjectId minic_parser_find_global_object_entity(\n")
patch("src/frontend/parser_global.c",
      "    parser->perf_find_global_calls += 1U;\n",
      """    parser->perf_find_global_calls += 1U;
#if defined(MINIC_TEST_FORCE_PARSER_HASH_FALLBACK)
    return minic_parser_find_global_object_linear_fallback(parser, name_span);
#endif
""")
patch("src/frontend/parser_global.c",
      """    if (!minic_parser_global_name_index_sync(parser) ||
        parser->perf_global_name_slot_capacity == 0U) {
        return MINIC_GLOBAL_OBJECT_INVALID;
    }""",
      """    if (!minic_parser_global_name_index_sync(parser) ||
        parser->perf_global_name_slot_capacity == 0U) {
        return minic_parser_find_global_object_linear_fallback(parser, name_span);
    }""")
patch("src/frontend/parser_global.c",
      """    return MINIC_GLOBAL_OBJECT_INVALID;
}

MinicGlobalObjectId minic_parser_find_global_object(""",
      """    return minic_parser_find_global_object_linear_fallback(parser, name_span);
}

MinicGlobalObjectId minic_parser_find_global_object(""")
# The hash code already scanned the local suffix in reverse. On hash-sync
# failure, only the remaining global prefix must be searched, still reverse.
enum_fallback = r'''static MinicEnumeratorId minic_parser_enum_global_linear_fallback(
    const MinicParser *parser, MinicSourceSpan span, size_t global_count) {
    size_t index;
    for (index = global_count; index > 0U; --index) {
        const MinicParserEnumConstant *constant =
            &parser->enum_constants[index - 1U];
        if (minic_parser_span_equals(parser, span, constant->name_span)) {
            return constant->enumerator_id;
        }
    }
    return MINIC_ENUMERATOR_INVALID;
}

'''
patch("src/frontend/parser_enum.c",
      "MinicEnumeratorId minic_parser_find_enum_constant(\n",
      enum_fallback + "MinicEnumeratorId minic_parser_find_enum_constant(\n")
patch("src/frontend/parser_enum.c",
      """    if (global_count == 0U) {
        return MINIC_ENUMERATOR_INVALID;
    }
    if (!minic_parser_enum_global_index_sync(parser, global_count) ||""",
      """    if (global_count == 0U) {
        return MINIC_ENUMERATOR_INVALID;
    }
#if defined(MINIC_TEST_FORCE_PARSER_HASH_FALLBACK)
    return minic_parser_enum_global_linear_fallback(
        parser, name_span, global_count);
#endif
    if (!minic_parser_enum_global_index_sync(parser, global_count) ||""")
patch("src/frontend/parser_enum.c",
      """    if (!minic_parser_enum_global_index_sync(parser, global_count) ||
        parser->perf_enum_name_slot_capacity == 0U) {
        return MINIC_ENUMERATOR_INVALID;
    }""",
      """    if (!minic_parser_enum_global_index_sync(parser, global_count) ||
        parser->perf_enum_name_slot_capacity == 0U) {
        return minic_parser_enum_global_linear_fallback(
            parser, name_span, global_count);
    }""")
patch("src/frontend/parser_enum.c",
      """    return MINIC_ENUMERATOR_INVALID;
}

bool minic_parser_bind_enum_constant(""",
      """    return minic_parser_enum_global_linear_fallback(
        parser, name_span, global_count);
}

bool minic_parser_bind_enum_constant(""")
print("MINIC_PERF_PARSER_HASH_FALLBACK_V1=APPLIED")
