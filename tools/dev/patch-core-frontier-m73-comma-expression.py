#!/usr/bin/env python3
"""Add Core IR lowering for the C comma operator."""

from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M73_COMMA_EXPRESSION_VALUE"


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M73 comma expression already applied")
        return 0

    anchor = '''    /* M58_LOGICAL_OR_VALUE: lower_condition_branch already owns the
       short-circuit semantics for both && and ||. Their value materialization
       is identical: branch to true/false, store 1/0, then reload. */
'''
    replacement = '''    /* M73_COMMA_EXPRESSION_VALUE: the left operand is sequenced for
       side effects and its scalar value is discarded; the right operand
       supplies the value of the whole comma expression. Unsupported left
       operand forms remain fail-closed through lower_expression(). */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_COMMA) {
        MinicCoreValueId discarded_left;
        MinicCoreLowerStatus status;

        status = lower_expression(context, expression->value.binary.left, &discarded_left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        return lower_expression(context, expression->value.binary.right, value_id);
    }

    /* M58_LOGICAL_OR_VALUE: lower_condition_branch already owns the
       short-circuit semantics for both && and ||. Their value materialization
       is identical: branch to true/false, store 1/0, then reload. */
'''
    count = text.count(anchor)
    if count != 1:
        raise SystemExit(f"M73 comma anchor count={count}")
    PATH.write_text(text.replace(anchor, replacement, 1))
    print("M73 comma expression lowering applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
