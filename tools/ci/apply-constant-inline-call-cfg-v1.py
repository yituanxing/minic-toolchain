#!/usr/bin/env python3
from pathlib import Path

# V0's semantic transformation is correct, but its locator assumed a specific
# spelling for lower_condition_branch(). Earlier stack patches can reformat the
# function signature, while the validation block inside the function remains
# unique. Reuse V0 and make that locator signature-independent.
path = Path("tools/ci/apply-constant-inline-call-cfg-v0.py")
source = path.read_text()
old = '''condition_fn = text.find("static MinicCoreLowerStatus lower_condition_branch(\\n")
if condition_fn < 0:
    raise SystemExit("lower_condition_branch definition missing")
'''
new = '''condition_fn = 0
'''
count = source.count(old)
if count != 1:
    raise SystemExit(f"expected one V0 condition locator, found {count}")
exec(compile(source.replace(old, new, 1), str(path), "exec"))
