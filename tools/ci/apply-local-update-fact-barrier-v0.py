#!/usr/bin/env python3
"""Conservative local-fact barrier at compound assignment and ++/-- lvalues.

Ordinary assignment invalidated compiler local facts, but compound assignment
and increments could bypass that kill. For inline specialization this can also
leave the specialized parameter incorrectly known after a runtime update.
Only direct local lvalues are involved; emitted runtime semantics are unchanged.
"""
from pathlib import Path

path=Path("src/core/core_lower.c")
text=path.read_text()
changes=[
    (
        """    if (minic_type_is_integer(stored_type) && minic_type_is_bool_integer(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (operand->kind == MINIC_EXPRESSION_MEMBER) {""",
        """    if (minic_type_is_integer(stored_type) && minic_type_is_bool_integer(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* LOCAL_MUTATING_FACT_BARRIER: ++/-- modifies a direct local even when
       its incoming value was a specialized integer constant. */
    if (operand->kind == MINIC_EXPRESSION_LOCAL) {
        core_local_constant_invalidate(context, operand->value.local_id);
    }
    if (operand->kind == MINIC_EXPRESSION_MEMBER) {""",
    ),
    (
        """        shift_assignment =
            expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||""",
        """        /* LOCAL_MUTATING_FACT_BARRIER: integer compound assignment
           modifies the direct target; it is not an ordinary '=' AST node. */
        if (target->kind == MINIC_EXPRESSION_LOCAL) {
            core_local_constant_invalidate(context, target->value.local_id);
        }
        shift_assignment =
            expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||""",
    ),
]
for old,new in changes:
    count=text.count(old)
    if count!=1:
        raise SystemExit(f"LOCAL_MUTATING_FACT_BARRIER anchor count={count} expected=1")
    text=text.replace(old,new,1)
path.write_text(text)
print("LOCAL_MUTATING_FACT_BARRIER=APPLIED paths=incdec,integer_compound")
