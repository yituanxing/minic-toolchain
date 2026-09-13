#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


# Integer specializations are transient compiler-generated body aliases.  They
# deliberately reuse the source FunctionBody/local ids, so they must not claim
# those locals as a second owner in the emission-reachability validation pass.
# The source function remains the sole semantic owner of the body graph.
replace_once(
    "src/frontend/function_body.c",
    "        if (!function->is_defined) {\n"
    "            continue;\n"
    "        }\n"
    "        if (function->body_block >= program->block_count ||\n",
    "        if (!function->is_defined) {\n"
    "            continue;\n"
    "        }\n"
    "        if (function->is_integer_specialization) {\n"
    "            continue;\n"
    "        }\n"
    "        if (function->body_block >= program->block_count ||\n",
)

# Keep reachability identity and body ownership identity separate.  A live
# specialization must be emitted under its own FunctionId, but its outgoing
# semantic edges are discovered by walking the source body.  Several variants
# of one source share that body, so process its edges once while preserving a
# distinct `reachable[]` bit for every emitted variant.
replace_once(
    "src/frontend/function_body.c",
    "    bool *reachable = NULL;\n"
    "    bool *processed = NULL;\n"
    "    MinicFunctionId *queue = NULL;\n",
    "    bool *reachable = NULL;\n"
    "    bool *processed = NULL;\n"
    "    bool *body_processed = NULL;\n"
    "    MinicFunctionId *queue = NULL;\n",
)

replace_once(
    "src/frontend/function_body.c",
    "    processed = calloc(program->function_count == 0U ? 1U : program->function_count,\n"
    "                       sizeof(*processed));\n"
    "    queue = malloc((program->function_count == 0U ? 1U : program->function_count) *\n"
    "                   sizeof(*queue));\n"
    "    if (reachable == NULL || processed == NULL || queue == NULL) {\n",
    "    processed = calloc(program->function_count == 0U ? 1U : program->function_count,\n"
    "                       sizeof(*processed));\n"
    "    body_processed = calloc(program->function_count == 0U ? 1U : program->function_count,\n"
    "                            sizeof(*body_processed));\n"
    "    queue = malloc((program->function_count == 0U ? 1U : program->function_count) *\n"
    "                   sizeof(*queue));\n"
    "    if (reachable == NULL || processed == NULL || body_processed == NULL || queue == NULL) {\n",
)

replace_once(
    "src/frontend/function_body.c",
    "    while (queue_cursor < queue_count) {\n"
    "        MinicFunctionId function_id = queue[queue_cursor++];\n"
    "        size_t expression_index;\n"
    "\n"
    "        if (processed[function_id]) {\n"
    "            continue;\n"
    "        }\n"
    "        processed[function_id] = true;\n"
    "        if (!validate_one_function(&validation, function_id)) {\n"
    "            goto done;\n"
    "        }\n",
    "    while (queue_cursor < queue_count) {\n"
    "        MinicFunctionId function_id = queue[queue_cursor++];\n"
    "        MinicFunctionId body_function_id = function_id;\n"
    "        const MinicFunction *queued_function = &program->functions[function_id];\n"
    "        size_t expression_index;\n"
    "\n"
    "        if (processed[function_id]) {\n"
    "            continue;\n"
    "        }\n"
    "        processed[function_id] = true;\n"
    "        if (queued_function->is_integer_specialization) {\n"
    "            body_function_id = queued_function->specialization_source;\n"
    "            if (body_function_id >= program->function_count ||\n"
    "                program->functions[body_function_id].is_integer_specialization) {\n"
    "                goto done;\n"
    "            }\n"
    "        }\n"
    "        if (body_processed[body_function_id]) {\n"
    "            continue;\n"
    "        }\n"
    "        if (!validate_one_function(&validation, body_function_id)) {\n"
    "            goto done;\n"
    "        }\n"
    "        body_processed[body_function_id] = true;\n",
)

replace_once(
    "src/frontend/function_body.c",
    "done:\n"
    "    free(queue);\n"
    "    free(processed);\n"
    "    free(reachable);\n",
    "done:\n"
    "    free(queue);\n"
    "    free(body_processed);\n"
    "    free(processed);\n"
    "    free(reachable);\n",
)

print("MINIC_INLINE_SPECIALIZATION_REACHABILITY_V0=APPLIED")
