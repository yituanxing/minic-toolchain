#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
old = '''static MinicCoreLowerStatus reload_scalar_value(MinicCoreLowerContext *context,\n                                                MinicSourceSpan span,\n                                                MinicType type,\n                                                MinicCoreObjectId object_id,\n                                                MinicCoreValueId *value_id);\n'''
new = old + '''static MinicCoreLowerStatus append_scalar_bitcast(MinicCoreLowerContext *context,\n                                                  MinicSourceSpan span,\n                                                  MinicType target_type,\n                                                  MinicCoreValueId source_value,\n                                                  MinicCoreValueId *value_id);\n'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"M31b fixup declaration anchor count={count}, expected 1")
p.write_text(text.replace(old, new, 1))
print("M31B_FIXUPS_APPLIED")
