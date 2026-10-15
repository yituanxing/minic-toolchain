#!/usr/bin/env python3
"""Stage M54: route effect-only conditional expression statements through Core lowering.

M53 taught lower_expression() how to build CFG for `cond ? void_a : void_b`, but
expression-statement lowering still rejected that expression kind before reaching
M53. Keep the statement layer thin: delegate the expression and require no value.
"""

from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M54_VOID_CONDITIONAL_STATEMENT"


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M54 void conditional expression statement already applied")
        return 0

    anchor = r'''    if (expression->kind == MINIC_EXPRESSION_CALL) {
        MinicCoreValueId discarded_value;

        return lower_direct_call(context, expression, &discarded_value);
    }
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
'''
    if text.count(anchor) != 1:
        raise SystemExit(f"M54 anchor count={text.count(anchor)}")

    replacement = r'''    if (expression->kind == MINIC_EXPRESSION_CALL) {
        MinicCoreValueId discarded_value;

        return lower_direct_call(context, expression, &discarded_value);
    }
    /* M54_VOID_CONDITIONAL_STATEMENT: expression statements are only an
       effect boundary. Once M53 can lower a void conditional expression, the
       statement layer must delegate rather than reject the expression kind. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        minic_type_is_void(expression->type)) {
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        status = lower_expression(context, statement->expression, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        return discarded_value == MINIC_CORE_VALUE_INVALID ? MINIC_CORE_LOWER_OK
                                                            : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
'''
    PATH.write_text(text.replace(anchor, replacement, 1))
    print("M54 void conditional expression statement applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
