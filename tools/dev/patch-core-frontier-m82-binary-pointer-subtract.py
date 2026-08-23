#!/usr/bin/env python3
"""Lower binary pointer subtraction through the existing Core pointer-offset primitive."""

from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M82_BINARY_POINTER_SUBTRACTION"


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M82 {name} anchor count={count}")
    return text.replace(old, new, 1)


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M82 binary pointer subtraction already applied")
        return 0

    condition = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        expression->value.binary.operator_kind == MINIC_BINARY_ADD &&\n        minic_type_is_pointer(expression->type)) {\n'''
    condition_new = '''    /* M82_BINARY_POINTER_SUBTRACTION: C/GNU pointer +/- integer share the\n       same scaled-offset primitive. Subtraction is only valid with the pointer\n       on the left; integer - pointer remains fail-closed. */\n    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||\n         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT) &&\n        minic_type_is_pointer(expression->type)) {\n'''
    text = replace_once(text, condition, condition_new, "pointer-binary-dispatch")

    symmetric = '''        } else if (minic_type_is_integer(left_expression->type) &&\n                   minic_type_is_pointer(right_expression->type)) {\n'''
    symmetric_new = '''        } else if (expression->value.binary.operator_kind == MINIC_BINARY_ADD &&\n                   minic_type_is_integer(left_expression->type) &&\n                   minic_type_is_pointer(right_expression->type)) {\n'''
    text = replace_once(text, symmetric, symmetric_new, "symmetric-add-only")

    payload = '''        instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;\n        instruction.value.pointer_offset.base = pointer_value;\n        instruction.value.pointer_offset.index = index_value;\n        instruction.value.pointer_offset.element_size = element_size;\n        return minic_core_function_append_value_instruction(\n'''
    payload_new = '''        instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;\n        instruction.value.pointer_offset.base = pointer_value;\n        instruction.value.pointer_offset.index = index_value;\n        instruction.value.pointer_offset.element_size = element_size;\n        /* M75 introduced this flag for compound subtraction. Always initialize\n           it on ordinary pointer arithmetic as well; leaving pointer + integer\n           indeterminate would make the Core program nondeterministic. */\n        instruction.value.pointer_offset.subtract =\n            expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT;\n        return minic_core_function_append_value_instruction(\n'''
    text = replace_once(text, payload, payload_new, "pointer-offset-payload")

    PATH.write_text(text)
    print("M82 binary pointer subtraction applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
