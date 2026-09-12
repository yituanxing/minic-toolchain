#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()

old = '''    MinicCoreLowerStatus status;
    MinicType stored_type;
    int source_kind;
    int source_operand_kind;
'''
new = '''    MinicCoreLowerStatus status;
    MinicType stored_type;
    MinicConstValue source_constant;
    bool source_constant_known;
    int source_kind;
    int source_operand_kind;
'''
if text.count(old) != 1:
    raise SystemExit(f"declaration anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old = '''    if (!minic_type_unqualified(target->type, &stored_type) ||
        !core_memory_scalar_type(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);
'''
new = '''    if (!minic_type_unqualified(target->type, &stored_type) ||
        !core_memory_scalar_type(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    source_constant_known =
        target->kind == MINIC_EXPRESSION_LOCAL && minic_type_is_integer(stored_type) &&
        core_const_eval_integer_with_locals(context, source_id, &source_constant);
    status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);
    /* GNU statement-expressions and other lowering-owned pure expressions may
       become a Core integer constant even when the source AST evaluator cannot
       classify the outer expression.  Observe that value before spill/reload;
       after spill it is represented by a LOAD and the constant identity is
       intentionally gone. */
    if (status == MINIC_CORE_LOWER_OK && !source_constant_known &&
        target->kind == MINIC_EXPRESSION_LOCAL && minic_type_is_integer(stored_type)) {
        source_constant_known =
            core_value_integer_constant(context, stored_value, &source_constant);
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"pre-spill anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old = '''    if (target->kind == MINIC_EXPRESSION_LOCAL) {
        MinicConstValue stored_constant;
        core_local_constant_invalidate(context, target->value.local_id);
        if (core_value_integer_constant(context, stored_value, &stored_constant)) {
            core_local_constant_set(context, target->value.local_id, &stored_constant);
        }
    }
'''
new = '''    if (target->kind == MINIC_EXPRESSION_LOCAL) {
        core_local_constant_invalidate(context, target->value.local_id);
        if (source_constant_known) {
            core_local_constant_set(context, target->value.local_id, &source_constant);
        }
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"post-store fact anchor count={text.count(old)}")
text = text.replace(old, new, 1)

p.write_text(text)
print("MINIC_LOCAL_ASSIGNMENT_SOURCE_CONSTANT_PATCH=APPLIED")
