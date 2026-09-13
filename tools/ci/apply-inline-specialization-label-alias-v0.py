#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/core/core_ir.h",
    "    MinicType *parameter_types;\n"
    "    size_t parameter_count;\n"
    "    MinicCoreEnumType *enum_types;\n",
    "    MinicType *parameter_types;\n"
    "    size_t parameter_count;\n"
    "    /* Integer-specialization clones share Semantic statement ids with their\n"
    "       source. Their executable Core block labels are already function-scoped,\n"
    "       so do not also export the source-id .Luser_* compatibility aliases. */\n"
    "    bool suppress_source_label_aliases;\n"
    "    MinicCoreEnumType *enum_types;\n",
)

replace_once(
    "src/core/core_lower.c",
    "    context.body = body;\n"
    "    context.source_function = source_function;\n"
    "    context.target = target;\n"
    "    context.function = &lowered;\n",
    "    context.body = body;\n"
    "    context.source_function = source_function;\n"
    "    context.target = target;\n"
    "    context.function = &lowered;\n"
    "    lowered.suppress_source_label_aliases = source_function->is_integer_specialization;\n",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    "        if (block->source_label_id != SIZE_MAX &&\n"
    "            fprintf(file, \".Luser_%zu:\\n\", block->source_label_id) < 0) {\n"
    "            return core_frame_fail(&frame);\n"
    "        }\n",
    "        if (!function->suppress_source_label_aliases &&\n"
    "            block->source_label_id != SIZE_MAX &&\n"
    "            fprintf(file, \".Luser_%zu:\\n\", block->source_label_id) < 0) {\n"
    "            return core_frame_fail(&frame);\n"
    "        }\n",
)

print("MINIC_INLINE_SPECIALIZATION_LABEL_ALIAS_V0=APPLIED")
