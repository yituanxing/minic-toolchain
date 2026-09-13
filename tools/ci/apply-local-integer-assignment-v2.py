#!/usr/bin/env python3
from pathlib import Path
import subprocess

# Start from the regression-safe V1: local facts are consulted only for
# compile-time control-flow reachability, never as a global expression-lowering
# shortcut.
subprocess.run(["python3", "tools/ci/apply-local-integer-conditions-v1.py"], check=True)

p = Path("src/core/core_lower.c")
text = p.read_text()

# Assignment facts must come from the source AST semantics, not from whether
# the already-lowered Core value happened to be a literal instruction.  This
# admits -22, casts/conversions and direct local reads without perturbing normal
# expression code generation.
old = '''    if (target->kind == MINIC_EXPRESSION_LOCAL) {
        MinicConstValue stored_constant;
        core_local_constant_invalidate(context, target->value.local_id);
        if (core_value_integer_constant(context, stored_value, &stored_constant)) {
            core_local_constant_set(context, target->value.local_id, &stored_constant);
        }
    }
'''
new = '''    if (target->kind == MINIC_EXPRESSION_LOCAL) {
        MinicConstValue stored_constant;
        core_local_constant_invalidate(context, target->value.local_id);
        if (core_const_eval_integer_with_locals(context, source_id, &stored_constant)) {
            core_local_constant_set(context, target->value.local_id, &stored_constant);
        }
    }
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one assignment fact block, found {count}")
text = text.replace(old, new, 1)

# V1 removed the broad lower_expression() fold.  After assignment facts switch
# to source-AST evaluation this old Core-literal helper has no users; remove it
# rather than weakening -Werror or keeping dead product code.
helper_start = text.find("static bool core_value_integer_constant(")
helper_end = text.find("static bool core_direct_local_read(", helper_start)
if helper_start < 0 or helper_end < 0 or helper_end <= helper_start:
    raise SystemExit("expected core_value_integer_constant helper before direct-local helper")
text = text[:helper_start] + text[helper_end:]

# GNU statement expressions commonly return a local whose value became known
# while lowering the selected compile-time branch.  Insert this only inside the
# local-aware evaluator, immediately after its logical-not case.  The longer
# anchor is deliberate: another binary-expression test exists elsewhere in the
# lowering file and must not be touched.
anchor = '''        value->type = expression->type;
        value->bits = is_zero ? 1U : 0U;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
'''
insert = '''        value->type = expression->type;
        value->bits = is_zero ? 1U : 0U;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT &&
        expression->value.statement_expression.result != MINIC_EXPRESSION_INVALID) {
        if (!core_const_eval_integer_with_locals(
                context, expression->value.statement_expression.result, &operand_value)) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &operand_value,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
'''
count = text.count(anchor)
if count != 1:
    raise SystemExit(f"expected one evaluator-local anchor, found {count}")
text = text.replace(anchor, insert, 1)

p.write_text(text)
print("MINIC_LOCAL_INTEGER_ASSIGNMENT_V2=APPLIED")
