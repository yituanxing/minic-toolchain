#!/usr/bin/env python3
from pathlib import Path

# Cache-generation note: this patch participates in the expanded Linux semantic
# stack hash. Keep the post-tail-promotion frontier separate from any mixed cache
# saved while the previous 73-object refresh was interrupted.
p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M180_NULL_POINTER_CONDITION_FACTS"
if marker in text:
    print("MINIC_LOCAL_NULL_POINTER_CONDITIONS_V0=ALREADY")
    raise SystemExit(0)

anchor = '''    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
'''
insert = '''    /* M180_NULL_POINTER_CONDITION_FACTS: local null-pointer tracking already
       records CONFIG-off stubs such as `p = helper_returning_NULL()`.  Consume
       that fact when C converts `!p` to int so a following terminating guard is
       folded before dead external calls are emitted. */
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        const MinicExpression *operand = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (operand != NULL && minic_type_is_pointer(operand->type) &&
            core_expression_known_null_pointer(
                context, expression->value.unary.operand)) {
            value->type = expression->type;
            value->bits = UINT64_C(1);
            return true;
        }
    }
'''
start = text.find("static bool core_const_eval_integer_with_locals(")
if start < 0:
    raise SystemExit("local evaluator missing")
pos = text.find(anchor, start)
if pos < 0:
    raise SystemExit("local evaluator cast anchor missing")
text = text[:pos] + insert + text[pos:]
p.write_text(text)
print("MINIC_LOCAL_NULL_POINTER_CONDITIONS_V0=APPLIED")
