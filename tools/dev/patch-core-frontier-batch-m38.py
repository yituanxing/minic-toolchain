#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M38 {label}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/core/core_lower.c",
    "        MinicType element_type;\n        MinicType pointer_type;\n        MinicArrayObjectInfo array_info;\n",
    "        MinicType element_type;\n        MinicType index_value_type;\n        MinicType pointer_type;\n        MinicArrayObjectInfo array_info;\n",
    "subscript value type declaration",
)

replace_once(
    "src/core/core_lower.c",
    "        if (index_value >= context->function->value_count ||\n"
    "            !minic_type_equal(context->function->values[index_value].type, index->type)) {\n"
    "            return MINIC_CORE_LOWER_ERROR;\n"
    "        }\n",
    "        /* Integer lvalue conversion drops top-level cv-qualifiers.  The\n"
    "         * Core SSA value therefore owns the unqualified value type, not\n"
    "         * the source lvalue's qualified object type. */\n"
    "        if (!minic_type_unqualified(index->type, &index_value_type)) {\n"
    "            return MINIC_CORE_LOWER_UNSUPPORTED;\n"
    "        }\n"
    "        if (index_value >= context->function->value_count ||\n"
    "            !minic_type_equal(context->function->values[index_value].type,\n"
    "                              index_value_type)) {\n"
    "            return MINIC_CORE_LOWER_ERROR;\n"
    "        }\n",
    "subscript lvalue-conversion invariant",
)

print("M38_PATCH_APPLIED")
