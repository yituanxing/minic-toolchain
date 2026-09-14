#!/usr/bin/env python3
from pathlib import Path

# V0 already knows how to recurse through CAST/CONVERSION/LVALUE_READ to the
# value that established a {0,1} boolean range.  Do not reject a call argument
# merely because normal call conversion has already changed its outer AST type
# from _Bool to the callee's integer parameter type.
path = Path("tools/ci/apply-inline-specialization-boolean-range-v0.py")
source = path.read_text()
old = '''            if (argument == NULL || !minic_type_is_bool_integer(argument->type) ||
                !minic_inline_boolean_range_argument(
'''
new = '''            if (argument == NULL ||
                !minic_inline_boolean_range_argument(
'''
count = source.count(old)
if count != 1:
    raise SystemExit(f"expected one direct boolean argument gate, found {count}")
source = source.replace(old, new, 1)
exec(compile(source, str(path), "exec"))
print("MINIC_INLINE_SPECIALIZATION_BOOLEAN_RANGE_V1=APPLIED")
