#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:180]!r}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/core/core_lower_internal.h",
    "typedef struct MinicCoreLocalIntegerConstant {\n"
    "    MinicConstValue value;\n"
    "    bool known;\n"
    "    bool escaped;\n"
    "} MinicCoreLocalIntegerConstant;\n",
    "typedef struct MinicCoreLocalIntegerConstant {\n"
    "    MinicConstValue value;\n"
    "    MinicConstValue specialization_value;\n"
    "    bool known;\n"
    "    bool escaped;\n"
    "    bool specialization_known;\n"
    "    bool specialization_modified;\n"
    "} MinicCoreLocalIntegerConstant;\n",
)

trace_helper = r'''

static bool core_inline_spec_fact_trace_enabled(const MinicCoreLowerContext *context) {
    const char *text;
    char *end;
    unsigned long long source_id;

    if (context == NULL || context->source_function == NULL ||
        !context->source_function->is_integer_specialization) {
        return false;
    }
    text = getenv("MINIC_INLINE_SPEC_FACT_TRACE_SOURCE");
    if (text == NULL || *text == '\0') {
        return false;
    }
    end = NULL;
    source_id = strtoull(text, &end, 10);
    return end != text && end != NULL && *end == '\0' &&
           source_id == (unsigned long long)context->source_function->specialization_source;
}
'''

replace_once(
    "src/core/core_lower.c",
    "static void core_local_constants_clear_known(MinicCoreLowerContext *context) {\n",
    trace_helper + "\nstatic void core_local_constants_clear_known(MinicCoreLowerContext *context) {\n",
)

replace_once(
    "src/core/core_lower.c",
    "    for (index = 0U; index < context->source_function->local_count; ++index) {\n"
    "        context->local_integer_constants[index].known = false;\n"
    "    }\n"
    "}\n\n"
    "static void core_local_constant_invalidate",
    "    for (index = 0U; index < context->source_function->local_count; ++index) {\n"
    "        MinicCoreLocalIntegerConstant *fact = &context->local_integer_constants[index];\n"
    "        if (fact->specialization_known && !fact->specialization_modified &&\n"
    "            !fact->escaped) {\n"
    "            fact->value = fact->specialization_value;\n"
    "            fact->known = true;\n"
    "        } else {\n"
    "            fact->known = false;\n"
    "        }\n"
    "        if (fact->specialization_known && core_inline_spec_fact_trace_enabled(context)) {\n"
    "            (void)fprintf(stderr,\n"
    "                          \"INLINE_SPEC_FACT event=clear function=%s source=%zu local=%zu known=%d modified=%d escaped=%d bits=%\" PRIu64 \"\\n\",\n"
    "                          context->source_function->name != NULL\n"
    "                              ? context->source_function->name\n"
    "                              : \"?\",\n"
    "                          (size_t)context->source_function->specialization_source,\n"
    "                          (size_t)(context->source_function->local_begin + index),\n"
    "                          fact->known ? 1 : 0,\n"
    "                          fact->specialization_modified ? 1 : 0,\n"
    "                          fact->escaped ? 1 : 0,\n"
    "                          fact->specialization_value.bits);\n"
    "        }\n"
    "    }\n"
    "}\n\n"
    "static void core_local_constant_invalidate",
)

replace_once(
    "src/core/core_lower.c",
    "    if (core_local_constant_index(context, local_id, &index)) {\n"
    "        context->local_integer_constants[index].known = false;\n"
    "    }\n"
    "}\n\n"
    "static void core_local_constant_escape",
    "    if (core_local_constant_index(context, local_id, &index)) {\n"
    "        MinicCoreLocalIntegerConstant *fact = &context->local_integer_constants[index];\n"
    "        fact->known = false;\n"
    "        if (fact->specialization_known) {\n"
    "            fact->specialization_modified = true;\n"
    "            if (core_inline_spec_fact_trace_enabled(context)) {\n"
    "                (void)fprintf(stderr,\n"
    "                              \"INLINE_SPEC_FACT event=invalidate function=%s source=%zu local=%zu bits=%\" PRIu64 \"\\n\",\n"
    "                              context->source_function->name != NULL\n"
    "                                  ? context->source_function->name\n"
    "                                  : \"?\",\n"
    "                              (size_t)context->source_function->specialization_source,\n"
    "                              (size_t)local_id,\n"
    "                              fact->specialization_value.bits);\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "}\n\n"
    "static void core_local_constant_escape",
)

replace_once(
    "src/core/core_lower.c",
    "    if (core_local_constant_index(context, local_id, &index)) {\n"
    "        context->local_integer_constants[index].known = false;\n"
    "        context->local_integer_constants[index].escaped = true;\n"
    "    }\n"
    "}\n\n"
    "static bool core_local_constant_get",
    "    if (core_local_constant_index(context, local_id, &index)) {\n"
    "        MinicCoreLocalIntegerConstant *fact = &context->local_integer_constants[index];\n"
    "        fact->known = false;\n"
    "        fact->escaped = true;\n"
    "        if (fact->specialization_known) {\n"
    "            fact->specialization_modified = true;\n"
    "            if (core_inline_spec_fact_trace_enabled(context)) {\n"
    "                (void)fprintf(stderr,\n"
    "                              \"INLINE_SPEC_FACT event=escape function=%s source=%zu local=%zu bits=%\" PRIu64 \"\\n\",\n"
    "                              context->source_function->name != NULL\n"
    "                                  ? context->source_function->name\n"
    "                                  : \"?\",\n"
    "                              (size_t)context->source_function->specialization_source,\n"
    "                              (size_t)local_id,\n"
    "                              fact->specialization_value.bits);\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "}\n\n"
    "static bool core_local_constant_get",
)

replace_once(
    "src/core/core_lower.c",
    "    if (value == NULL || !core_local_constant_index(context, local_id, &index) ||\n"
    "        !context->local_integer_constants[index].known ||\n"
    "        context->local_integer_constants[index].escaped || context->body == NULL ||\n",
    "    if (core_local_constant_index(context, local_id, &index) &&\n"
    "        context->local_integer_constants[index].specialization_known &&\n"
    "        core_inline_spec_fact_trace_enabled(context)) {\n"
    "        const MinicCoreLocalIntegerConstant *fact = &context->local_integer_constants[index];\n"
    "        (void)fprintf(stderr,\n"
    "                      \"INLINE_SPEC_FACT event=get function=%s source=%zu local=%zu known=%d modified=%d escaped=%d bits=%\" PRIu64 \"\\n\",\n"
    "                      context->source_function->name != NULL\n"
    "                          ? context->source_function->name\n"
    "                          : \"?\",\n"
    "                      (size_t)context->source_function->specialization_source,\n"
    "                      (size_t)local_id,\n"
    "                      fact->known ? 1 : 0,\n"
    "                      fact->specialization_modified ? 1 : 0,\n"
    "                      fact->escaped ? 1 : 0,\n"
    "                      fact->specialization_value.bits);\n"
    "    }\n"
    "    if (value == NULL || !core_local_constant_index(context, local_id, &index) ||\n"
    "        !context->local_integer_constants[index].known ||\n"
    "        context->local_integer_constants[index].escaped || context->body == NULL ||\n",
)

replace_once(
    "src/core/core_lower.c",
    "                core_local_constant_set(context, local_id, &specialized_value);\n"
    "                if (getenv(\"MINIC_LOCAL_FACT_TRACE\") != NULL) {\n",
    "                core_local_constant_set(context, local_id, &specialized_value);\n"
    "                {\n"
    "                    size_t specialized_index;\n"
    "                    if (core_local_constant_index(context, local_id, &specialized_index) &&\n"
    "                        context->local_integer_constants[specialized_index].known) {\n"
    "                        MinicCoreLocalIntegerConstant *fact =\n"
    "                            &context->local_integer_constants[specialized_index];\n"
    "                        fact->specialization_value = fact->value;\n"
    "                        fact->specialization_known = true;\n"
    "                        fact->specialization_modified = false;\n"
    "                        if (core_inline_spec_fact_trace_enabled(context)) {\n"
    "                            (void)fprintf(stderr,\n"
    "                                          \"INLINE_SPEC_FACT event=seed function=%s source=%zu local=%zu bits=%\" PRIu64 \"\\n\",\n"
    "                                          context->source_function->name != NULL\n"
    "                                              ? context->source_function->name\n"
    "                                              : \"?\",\n"
    "                                          (size_t)context->source_function->specialization_source,\n"
    "                                          (size_t)local_id,\n"
    "                                          fact->specialization_value.bits);\n"
    "                        }\n"
    "                    }\n"
    "                }\n"
    "                if (getenv(\"MINIC_LOCAL_FACT_TRACE\") != NULL) {\n",
)

print("MINIC_INLINE_SPECIALIZATION_STABLE_PARAMETER_FACTS_V0=APPLIED")
