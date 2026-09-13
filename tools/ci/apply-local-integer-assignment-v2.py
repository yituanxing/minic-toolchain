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

# GNU statement expressions commonly return a local whose value became known
# while lowering the selected compile-time branch.  At the outer assignment,
# preserve that fact by evaluating only the statement-expression result node.
anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
'''
insert = '''    if (expression->kind == MINIC_EXPRESSION_STATEMENT &&
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
    raise SystemExit(f"expected one evaluator binary anchor, found {count}")
text = text.replace(anchor, insert, 1)

p.write_text(text)
print("MINIC_LOCAL_INTEGER_ASSIGNMENT_V2=APPLIED")
