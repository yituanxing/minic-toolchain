#!/usr/bin/env python3
from pathlib import Path

# Some earlier Linux closures already admit DEREFERENCE in the CFG purity
# walker. Keep this patch as an idempotent compatibility layer. This file is
# exec()'d from the composite semantic stack, so the ALREADY path must not
# raise SystemExit(0): doing so would terminate the parent stack before later
# closures (notably the annihilator-priority and tail passes) run.
p = Path("src/core/core_lower.c")
text = p.read_text()

fn = "static bool core_cfg_pure_call_argument("
start = text.find(fn)
if start < 0:
    raise SystemExit("CFG purity helper missing")
end = text.find("\nstatic bool core_cfg_constant_inline_call(", start)
if end < 0:
    raise SystemExit("CFG purity helper end missing")
body = text[start:end]

if "case MINIC_EXPRESSION_DEREFERENCE:" in body:
    print("MINIC_PURE_DEREFERENCE_CFG_V0=ALREADY")
else:
    old = '''    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_ADDRESS_OF:
    case MINIC_EXPRESSION_LVALUE_READ:
        return core_cfg_pure_call_argument(
            context, expression->value.unary.operand, depth + 1U);
'''
    new = '''    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_ADDRESS_OF:
    case MINIC_EXPRESSION_DEREFERENCE: /* M188_PURE_DEREFERENCE_CFG */
    case MINIC_EXPRESSION_LVALUE_READ:
        return core_cfg_pure_call_argument(
            context, expression->value.unary.operand, depth + 1U);
'''
    count = body.count(old)
    if count != 1:
        raise SystemExit(f"CFG purity unary anchor: expected one, found {count}")
    body = body.replace(old, new, 1)
    text = text[:start] + body + text[end:]
    p.write_text(text)
    print("MINIC_PURE_DEREFERENCE_CFG_V0=APPLIED nonvolatile=1")
