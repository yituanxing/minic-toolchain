#!/usr/bin/env python3
"""Stage M58: materialize short-circuit logical OR through existing Core CFG lowering."""

from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M58_LOGICAL_OR_VALUE"


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M58 logical OR value already applied")
        return 0

    old = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND) {\n        MinicCoreBlockId false_block;\n'''
    new = '''    /* M58_LOGICAL_OR_VALUE: lower_condition_branch already owns the\n       short-circuit semantics for both && and ||. Their value materialization\n       is identical: branch to true/false, store 1/0, then reload. */\n    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||\n         expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR)) {\n        MinicCoreBlockId false_block;\n'''
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M58 value anchor count={count}")
    PATH.write_text(text.replace(old, new, 1))
    print("M58 logical OR value applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
