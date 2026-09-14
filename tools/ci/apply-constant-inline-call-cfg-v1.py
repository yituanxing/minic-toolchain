#!/usr/bin/env python3
from pathlib import Path

# V0's semantic transformation is correct, but its locator assumed a specific
# spelling for lower_condition_branch(). Earlier stack patches can reformat the
# function signature, while the validation block inside the function remains
# unique. Reuse V0 and make that locator signature-independent.  Also allow a
# non-volatile dereference/lvalue read to count as a side-effect-free call
# argument for CFG-only constant folding.  This is required for Linux helpers
# such as pmd_trans_huge(*pmd): evaluating the value load has no C-visible side
# effect, and the called internal inline predicate can still be a constant.
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
source = source.replace(old, new, 1)

pure_old = '''    case MINIC_EXPRESSION_ADDRESS_OF:\n    case MINIC_EXPRESSION_LVALUE_READ:\n        return core_cfg_pure_call_argument(\n            context, expression->value.unary.operand, depth + 1U);\n'''
pure_new = '''    case MINIC_EXPRESSION_ADDRESS_OF:\n    case MINIC_EXPRESSION_DEREFERENCE:\n    case MINIC_EXPRESSION_LVALUE_READ:\n        return core_cfg_pure_call_argument(\n            context, expression->value.unary.operand, depth + 1U);\n'''
count = source.count(pure_old)
if count != 1:
    raise SystemExit(f"expected one V0 pure unary/lvalue block, found {count}")
source = source.replace(pure_old, pure_new, 1)

exec(compile(source, str(path), "exec"))

multi = Path("tools/ci/apply-constant-inline-multireturn-v0.py")
exec(compile(multi.read_text(), str(multi), "exec"))

# Temporary diagnostic on the focused diagnose branch only.
probe = Path("tools/ci/apply-constant-inline-call-cfg-probe-v0.py")
exec(compile(probe.read_text(), str(probe), "exec"))
