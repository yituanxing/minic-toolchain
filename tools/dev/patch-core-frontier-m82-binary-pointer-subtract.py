#!/usr/bin/env python3
# Keep M82 productized and advance the hot frontier through M83.

from pathlib import Path
from runpy import run_path

PATH = Path("src/core/core_lower.c")
MARKER = "M82_BINARY_POINTER_SUBTRACTION"
M83_DRIVER = Path("tools/dev/patch-core-frontier-m83-indirect-call.py")


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M82 {name} anchor count={count}")
    return text.replace(old, new, 1)


def apply_m82_if_needed() -> None:
    text = PATH.read_text()
    if MARKER in text:
        print("M82 binary pointer subtraction already applied")
        return

    condition = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
        minic_type_is_pointer(expression->type)) {
'''
    condition_new = '''    /* M82_BINARY_POINTER_SUBTRACTION: C/GNU pointer +/- integer share the
       same scaled-offset primitive. Subtraction is only valid with the pointer
       on the left; integer - pointer remains fail-closed. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT) &&
        minic_type_is_pointer(expression->type)) {
'''
    text = replace_once(text, condition, condition_new, "pointer-binary-dispatch")

    symmetric = '''        } else if (minic_type_is_integer(left_expression->type) &&
                   minic_type_is_pointer(right_expression->type)) {
'''
    symmetric_new = '''        } else if (expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
                   minic_type_is_integer(left_expression->type) &&
                   minic_type_is_pointer(right_expression->type)) {
'''
    text = replace_once(text, symmetric, symmetric_new, "symmetric-add-only")

    payload = '''        instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        instruction.value.pointer_offset.base = pointer_value;
        instruction.value.pointer_offset.index = index_value;
        instruction.value.pointer_offset.element_size = element_size;
        return minic_core_function_append_value_instruction(
'''
    payload_new = '''        instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        instruction.value.pointer_offset.base = pointer_value;
        instruction.value.pointer_offset.index = index_value;
        instruction.value.pointer_offset.element_size = element_size;
        /* M75 introduced this flag for compound subtraction. Always initialize
           it on ordinary pointer arithmetic as well; leaving pointer + integer
           indeterminate would make the Core program nondeterministic. */
        instruction.value.pointer_offset.subtract =
            expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT;
        return minic_core_function_append_value_instruction(
'''
    text = replace_once(text, payload, payload_new, "pointer-offset-payload")
    PATH.write_text(text)
    print("M82 binary pointer subtraction applied")


def main() -> int:
    apply_m82_if_needed()
    namespace = run_path(str(M83_DRIVER))
    m83_main = namespace.get("main")
    if not callable(m83_main):
        raise SystemExit("M83 frontier driver does not expose main()")
    return int(m83_main())


if __name__ == "__main__":
    raise SystemExit(main())
