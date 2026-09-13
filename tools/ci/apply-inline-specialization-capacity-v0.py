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
    "src/compiler/compiler.c",
    "#define MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT 256U",
    "#define MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT 2048U",
)

replace_once(
    "src/compiler/compiler.c",
    "        if (specialized_id == MINIC_FUNCTION_INVALID) {\n"
    "            if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT ||\n"
    "                !minic_add_inline_integer_specialization(program, source_id, known, bits,\n"
    "                                                         specialization_count,\n"
    "                                                         &specialized_id)) {\n"
    "                return false;\n"
    "            }\n"
    "            specialization_count += 1U;\n"
    "        }\n",
    "        if (specialized_id == MINIC_FUNCTION_INVALID) {\n"
    "            if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT) {\n"
    "                (void)fprintf(stderr,\n"
    "                              \"MINIC_INLINE_INTEGER_SPECIALIZATION_V0_FAIL reason=limit clones=%zu limit=%u expression=%zu source=%zu\\n\",\n"
    "                              specialization_count,\n"
    "                              (unsigned int)MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT,\n"
    "                              expression_index,\n"
    "                              (size_t)source_id);\n"
    "                return false;\n"
    "            }\n"
    "            if (!minic_add_inline_integer_specialization(program, source_id, known, bits,\n"
    "                                                         specialization_count,\n"
    "                                                         &specialized_id)) {\n"
    "                (void)fprintf(stderr,\n"
    "                              \"MINIC_INLINE_INTEGER_SPECIALIZATION_V0_FAIL reason=add clones=%zu expression=%zu source=%zu params=%zu locals=%zu\\n\",\n"
    "                              specialization_count,\n"
    "                              expression_index,\n"
    "                              (size_t)source_id,\n"
    "                              source.parameter_count,\n"
    "                              source.local_count);\n"
    "                return false;\n"
    "            }\n"
    "            specialization_count += 1U;\n"
    "        }\n",
)

print("MINIC_INLINE_SPECIALIZATION_CAPACITY_V0=APPLIED limit=2048")
