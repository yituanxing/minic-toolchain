#!/usr/bin/env python3
# Keep M82 productized and advance the hot frontier through M83/M83b/M84/M85/M86.

from pathlib import Path
from runpy import run_path

PATH = Path("src/core/core_lower.c")
MARKER = "M82_BINARY_POINTER_SUBTRACTION"
M83_DRIVER = Path("tools/dev/patch-core-frontier-m83-indirect-call.py")
M83B_DRIVER = Path("tools/dev/patch-core-frontier-m83b-call-statement-dispatch.py")
M84_DRIVER = Path("tools/dev/patch-core-frontier-m84-pointer-loop-condition.py")
M85_DRIVER = Path("tools/dev/patch-core-frontier-m85-record-call-argument.py")
M85B_DRIVER = Path("tools/dev/patch-core-frontier-m85b-record-callee-verifier.py")
M86_DRIVER = Path("tools/dev/patch-core-frontier-m86-record-call-result.py")
M83_IR = Path("src/core/core_ir.h")
M83_IR_IMPL = Path("src/core/core_ir.c")
M83_LOWER = Path("src/core/core_lower.c")
M83_CODEGEN = Path("src/target/riscv64/core_codegen.c")


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


def run_driver(path: Path, name: str) -> int:
    namespace = run_path(str(path))
    driver_main = namespace.get("main")
    if not callable(driver_main):
        raise SystemExit(f"{name} frontier driver does not expose main()")
    return int(driver_main())


def m83_productized() -> bool:
    return (
        "MINIC_CORE_INSTRUCTION_INDIRECT_CALL" in M83_IR.read_text()
        and "minic_core_function_add_call_signature" in M83_IR_IMPL.read_text()
        and "lower_indirect_call(" in M83_LOWER.read_text()
        and "emit_indirect_call(" in M83_CODEGEN.read_text()
    )


def main() -> int:
    apply_m82_if_needed()
    if m83_productized():
        print("M83 first-class indirect call already productized")
    else:
        status = run_driver(M83_DRIVER, "M83")
        if status != 0:
            return status
    status = run_driver(M83B_DRIVER, "M83b")
    if status != 0:
        return status
    status = run_driver(M84_DRIVER, "M84")
    if status != 0:
        return status
    status = run_driver(M85_DRIVER, "M85")
    if status != 0:
        return status
    status = run_driver(M85B_DRIVER, "M85b")
    if status != 0:
        return status
    return run_driver(M86_DRIVER, "M86")


if __name__ == "__main__":
    raise SystemExit(main())
