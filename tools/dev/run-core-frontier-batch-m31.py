#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys

patcher = Path("tools/dev/patch-core-frontier-batch-m31.py")
text = patcher.read_text()
old = '''    "    for (index = 0U; index < function->callee_count; ++index) {\\n",
    r''' + "'''    for (index = 0U; index < function->fixed_register_count; ++index) {"
new = '''    "    for (index = 0U; index < function->callee_count; ++index) {\\n"
    "        const MinicCoreCallee *callee;\\n"
    "        size_t parameter_index;\\n\\n",
    r''' + "'''    for (index = 0U; index < function->fixed_register_count; ++index) {"
if text.count(old) != 1:
    raise SystemExit(f"M31 wrapper expected one ambiguous verifier anchor, found {text.count(old)}")
patcher.write_text(text.replace(old, new, 1))
subprocess.run([sys.executable, str(patcher)], check=True)
print("M31_PATCHER_ANCHOR_FIXED")
