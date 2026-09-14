#!/usr/bin/env python3
from pathlib import Path

# V0's semantic transformation is correct, but its locator assumed a specific
# spelling for lower_condition_branch(). Earlier stack patches can reformat the
# function signature, while the validation block inside the function remains
# unique. Reuse V0 and make that locator signature-independent.  Also allow a
# non-volatile dereference/lvalue read to count as a side-effect-free call
# argument for CFG-only constant folding.  This is required for helpers such as
# pmd_trans_huge(*pmd): evaluating the value load has no C-visible side effect,
# and the called internal inline predicate can still be a constant.
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

# Algebraic CFG facts where the result is known without knowing the other
# operand.  Only consume the unknown side when it is side-effect free.
p = Path("src/core/core_lower.c")
text = p.read_text()
fn_anchor = "static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,\n"
start = text.find(fn_anchor)
if start < 0:
    raise SystemExit("local integer evaluator missing for annihilator closure")
anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||\n'''
pos = text.find(anchor, start)
if pos < 0:
    raise SystemExit("comparison anchor missing for annihilator closure")
annihilator = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY)) {
        MinicConstValue known;
        bool is_zero;

        if (core_const_eval_integer_with_locals(
                context, expression->value.binary.left, &known) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &known,
                                      &is_zero) &&
            is_zero &&
            core_cfg_pure_call_argument(
                context, expression->value.binary.right, 0U)) {
            value->type = expression->type;
            value->bits = 0U;
            return true;
        }
        if (core_const_eval_integer_with_locals(
                context, expression->value.binary.right, &known) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &known,
                                      &is_zero) &&
            is_zero &&
            core_cfg_pure_call_argument(
                context, expression->value.binary.left, 0U)) {
            value->type = expression->type;
            value->bits = 0U;
            return true;
        }
    }
'''
text = text[:pos] + annihilator + text[pos:]
p.write_text(text)
print("MINIC_CONSTANT_CFG_ANNIHILATOR_V0=APPLIED")

# Keep the focused probe active in batch evidence until this undefined cluster
# is fully closed; it is stderr-only and does not alter generated code.
probe = Path("tools/ci/apply-constant-inline-call-cfg-probe-v0.py")
exec(compile(probe.read_text(), str(probe), "exec"))
