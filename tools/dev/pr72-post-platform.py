#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_expression.c")
text = path.read_text()
old = """        target_expression = minic_c0_program_expression(parser->program, left);
        if (target_expression == NULL ||
            target_expression->value_category != MINIC_VALUE_LVALUE ||
"""
new = """        target_expression = minic_c0_program_expression(parser->program, left);
        if (target_expression != NULL && minic_type_is_record(target_expression->type)) {
            /* Record assignment already has statement-level recursive copy lowering.
               Leave '=' unconsumed so that path can handle standalone record copies. */
            *expression_id = left;
            return true;
        }
        if (target_expression == NULL ||
            target_expression->value_category != MINIC_VALUE_LVALUE ||
"""
if old not in text:
    raise SystemExit("missing assignment-expression target check")
path.write_text(text.replace(old, new, 1))
print("staged record-assignment routing")
