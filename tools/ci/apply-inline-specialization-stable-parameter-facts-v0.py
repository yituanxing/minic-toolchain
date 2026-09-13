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
    "        }\n"
    "    }\n"
    "}\n\n"
    "static bool core_local_constant_get",
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
    "                    }\n"
    "                }\n"
    "                if (getenv(\"MINIC_LOCAL_FACT_TRACE\") != NULL) {\n",
)

print("MINIC_INLINE_SPECIALIZATION_STABLE_PARAMETER_FACTS_V0=APPLIED")
