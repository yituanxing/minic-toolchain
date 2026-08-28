#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M86B_RECORD_ASSIGNMENT_EXPRESSION_STATEMENT"


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M86b record assignment expression statement already applied")
        return 0

    anchor = '''    /* M83B_CALL_STATEMENT_DISPATCH: CALL ownership lives in lower_expression().\n       Statement context only discards the produced value; it must not force an\n       indirect call back through the legacy direct-call helper. */\n'''
    if text.count(anchor) != 1:
        raise SystemExit(f"M86b expression-statement anchor count={text.count(anchor)}")

    insertion = '''    /* M86B_RECORD_ASSIGNMENT_EXPRESSION_STATEMENT: a record assignment used as\n       an expression statement has the same storage effect as RECORD_COPY; its\n       aggregate expression result is discarded, so Core does not need an\n       aggregate SSA value. This also lets M86 direct-record-call result objects\n       feed ordinary `lhs = call_returning_record()` statements. */\n    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT &&\n        minic_type_is_record(expression->type)) {\n        const MinicExpression *record_target;\n        const MinicExpression *record_source;\n        MinicStatement record_copy;\n\n        record_target = minic_c0_program_expression(\n            context->body->program, expression->value.binary.left);\n        record_source = minic_c0_program_expression(\n            context->body->program, expression->value.binary.right);\n        if (record_target == NULL || record_source == NULL ||\n            !minic_type_is_record(record_target->type) ||\n            !minic_type_is_record(record_source->type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        record_copy = *statement;\n        record_copy.kind = MINIC_STATEMENT_RECORD_COPY;\n        record_copy.span = expression->span;\n        record_copy.target_expression = expression->value.binary.left;\n        record_copy.expression = expression->value.binary.right;\n        return lower_record_copy_statement(context, &record_copy);\n    }\n\n'''
    PATH.write_text(text.replace(anchor, insertion + anchor, 1))
    print("M86b record assignment expression statement applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
