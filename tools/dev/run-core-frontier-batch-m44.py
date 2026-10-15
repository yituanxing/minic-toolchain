#!/usr/bin/env python3
from pathlib import Path
import runpy

p = Path("tools/dev/patch-core-frontier-batch-m44.py")
text = p.read_text()
old = '''replace_once(
    "src/core/core_lower.c",
    ''' + "'''" + '''    if (expression->kind == MINIC_EXPRESSION_DISCARD) {\\n''' + "'''" + ''',
    ''' + "'''" + '''    if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS) {\\n'''
new = '''replace_once(
    "src/core/core_lower.c",
    ''' + "'''" + '''    if (expression->value_category != MINIC_VALUE_RVALUE) {\\n        return MINIC_CORE_LOWER_UNSUPPORTED;\\n    }\\n    if (expression->kind == MINIC_EXPRESSION_DISCARD) {\\n''' + "'''" + ''',
    ''' + "'''" + '''    if (expression->value_category != MINIC_VALUE_RVALUE) {\\n        return MINIC_CORE_LOWER_UNSUPPORTED;\\n    }\\n    if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS) {\\n'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"M44 runner anchor: expected one patcher block, found {count}")
p.write_text(text.replace(old, new, 1))
runpy.run_path(str(p), run_name="__main__")
