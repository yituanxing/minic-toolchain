#!/usr/bin/env python3
# Route CALL expression statements through the generic expression dispatcher.

from pathlib import Path

MARKER = "M83B_CALL_STATEMENT_DISPATCH"
LOWER = Path("src/core/core_lower.c")


def main() -> int:
    text = LOWER.read_text()
    if MARKER in text:
        print("M83b call statement dispatch already applied")
        return 0

    old = '''    if (expression->kind == MINIC_EXPRESSION_CALL) {
        MinicCoreValueId discarded_value;

        return lower_direct_call(context, expression, &discarded_value);
    }
'''
    new = '''    /* M83B_CALL_STATEMENT_DISPATCH: CALL ownership lives in lower_expression().
       Statement context only discards the produced value; it must not force an
       indirect call back through the legacy direct-call helper. */
    if (expression->kind == MINIC_EXPRESSION_CALL) {
        MinicCoreValueId discarded_value;

        return lower_expression(context, statement->expression, &discarded_value);
    }
'''
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M83b call-statement anchor count={count}")
    LOWER.write_text(text.replace(old, new, 1))
    print("M83b call statement dispatch applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
