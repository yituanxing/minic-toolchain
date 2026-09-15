#!/usr/bin/env python3
from pathlib import Path

# core_cfg_pure_call_argument is used only to justify CFG-only algebraic
# identities such as X & 0 -> 0 and reverse short-circuit folding.  Forming a
# dereference lvalue is side-effect-free when its operand is pure; an actual
# volatile read remains rejected by the function's existing volatile-type gate.
# The missing DEREFERENCE case prevented CONFIG-off expressions such as
# mapping->host->i_flags & 0 from reaching the existing annihilator rule.
p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M188_PURE_DEREFERENCE_CFG"
if marker in text:
    print("MINIC_PURE_DEREFERENCE_CFG_V0=ALREADY")
    raise SystemExit(0)

fn = "static bool core_cfg_pure_call_argument("
start = text.find(fn)
if start < 0:
    raise SystemExit("CFG purity helper missing")
end = text.find("\nstatic bool core_cfg_constant_inline_call(", start)
if end < 0:
    raise SystemExit("CFG purity helper end missing")
body = text[start:end]
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
