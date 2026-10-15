#!/usr/bin/env python3
"""Add the forward declaration required by the staged M49 lower_address path."""

from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
prototype = """static MinicCoreLowerStatus append_scalar_bitcast(MinicCoreLowerContext *context,\n                                                  MinicSourceSpan span,\n                                                  MinicType target_type,\n                                                  MinicCoreValueId source_value,\n                                                  MinicCoreValueId *value_id);\n"""
if prototype in text:
    print("M49 scalar-bitcast forward declaration already present")
    raise SystemExit(0)
anchor = """static MinicCoreLowerStatus reload_scalar_value(MinicCoreLowerContext *context,\n                                                MinicSourceSpan span,\n                                                MinicType type,\n                                                MinicCoreObjectId object_id,\n                                                MinicCoreValueId *value_id);\n"""
if text.count(anchor) != 1:
    raise SystemExit(f"M49 forward anchor count={text.count(anchor)}")
path.write_text(text.replace(anchor, anchor + prototype, 1))
print("M49 scalar-bitcast forward declaration applied")
