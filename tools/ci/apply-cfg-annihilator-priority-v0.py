#!/usr/bin/env python3
from pathlib import Path

# M182 introduced safe X&0 / X*0 CFG annihilators guarded by expression purity,
# but later arithmetic closure placed strict two-constant BITWISE_AND/MULTIPLY
# handlers before them.  Those handlers return false as soon as X is unknown,
# making the annihilator code unreachable.  Preserve the existing rule exactly;
# only move it before strict binary folding.
p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M189_CFG_ANNIHILATOR_PRIORITY"
if marker in text:
    print("MINIC_CFG_ANNIHILATOR_PRIORITY_V0=ALREADY")
    raise SystemExit(0)

fn = "static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,\n"
# Skip possible forward declarations; select the occurrence whose signature is
# followed by a function body rather than a semicolon.
search = 0
start = -1
while True:
    pos = text.find(fn, search)
    if pos < 0:
        break
    sig_end = text.find(")", pos)
    brace = text.find("{", pos, sig_end + 8 if sig_end >= 0 else pos)
    semi = text.find(";", pos, sig_end + 8 if sig_end >= 0 else pos)
    if brace >= 0 and (semi < 0 or brace < semi):
        start = pos
        break
    search = pos + len(fn)
if start < 0:
    raise SystemExit("integer-with-locals evaluator definition missing")
end = text.find("\nstatic ", start + len(fn))
if end < 0:
    raise SystemExit("integer-with-locals evaluator end missing")
body = text[start:end]

ann_anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY)) {
        MinicConstValue known;
'''
ann_start = body.find(ann_anchor)
unary_anchor = '''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_PLUS ||
'''
ann_end = body.find(unary_anchor, ann_start)
if ann_start < 0 or ann_end < 0:
    raise SystemExit("CFG annihilator block missing")
ann_block = body[ann_start:ann_end]

strict_anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
'''
strict_start = body.find(strict_anchor)
if strict_start < 0 or strict_start >= ann_start:
    raise SystemExit("strict arithmetic block missing before annihilator")

body_without = body[:ann_start] + body[ann_end:]
strict_start = body_without.find(strict_anchor)
priority_block = '''    /* M189_CFG_ANNIHILATOR_PRIORITY: the unknown operand must not be
       rejected by strict two-constant arithmetic before the existing pure
       X&0 / X*0 identity gets a chance to prove the result. */\n''' + ann_block
body = body_without[:strict_start] + priority_block + body_without[strict_start:]
text = text[:start] + body + text[end:]
p.write_text(text)
print("MINIC_CFG_ANNIHILATOR_PRIORITY_V0=APPLIED bitand=1 multiply=1")
