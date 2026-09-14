#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
needle = "return MINIC_CORE_LOWER_UNSUPPORTED;"
count = text.count(needle)
if count == 0:
    raise SystemExit("no Core unsupported returns found")
replacement = '''do {\n            (void)fprintf(stderr,\n                          "CORE_UNSUPPORTED_TRACE helper=%s line=%d\\n",\n                          __func__, __LINE__);\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        } while (0);'''
text = text.replace(needle, replacement)
p.write_text(text)
print(f"MINIC_CORE_UNSUPPORTED_TRACE_V0=APPLIED returns={count}")
