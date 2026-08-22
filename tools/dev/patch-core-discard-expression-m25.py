#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]
core = root / "src/core/core_lower.c"
gate = root / ".github/scripts/compiler-c0-full-gate.sh"

text = core.read_text()
anchor = """    if (expression->value_category != MINIC_VALUE_RVALUE) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {\n"""
replacement = """    if (expression->value_category != MINIC_VALUE_RVALUE) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    if (expression->kind == MINIC_EXPRESSION_DISCARD) {\n        const MinicExpression *operand;\n        MinicCoreValueId discarded_value;\n        MinicCoreLowerStatus status;\n\n        if (!minic_type_is_void(expression->type)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        operand = minic_c0_program_expression(context->body->program,\n                                              expression->value.unary.operand);\n        if (operand == NULL) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        status = lower_expression(context, expression->value.unary.operand, &discarded_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (minic_type_is_void(operand->type)) {\n            if (discarded_value != MINIC_CORE_VALUE_INVALID) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n        } else if (discarded_value == MINIC_CORE_VALUE_INVALID ||\n                   discarded_value >= context->function->value_count) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        *value_id = MINIC_CORE_VALUE_INVALID;\n        return MINIC_CORE_LOWER_OK;\n    }\n    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {\n"""
text = replace_once(text, anchor, replacement, "lower_expression discard")

anchor = """    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {\n        MinicCoreValueId discarded_value;\n\n        return lower_expression(context, statement->expression, &discarded_value);\n    }\n"""
replacement = anchor + """    if (expression->kind == MINIC_EXPRESSION_DISCARD) {\n        MinicCoreValueId discarded_value;\n\n        return lower_expression(context, statement->expression, &discarded_value);\n    }\n"""
text = replace_once(text, anchor, replacement, "expression-statement discard")
core.write_text(text)

focused_c = root / "tests/compiler/c0/core_discard_expression_m25.c"
focused_c.write_text(
    """static unsigned int core_m25_touch(unsigned int *value) {\n"
    "    *value = *value + 3U;\n"
    "    return *value;\n"
    "}\n\n"
    "void core_m25_discard_pointer(unsigned int *value) {\n"
    "    (void)(value);\n"
    "}\n\n"
    "void core_m25_discard_call(unsigned int *value) {\n"
    "    (void)core_m25_touch(value);\n"
    "}\n\n"
    "unsigned int *core_m25_le32_shape(unsigned int *buf, unsigned int words) {\n"
    "    while (words--) {\n"
    "        do {\n"
    "            (void)(buf);\n"
    "        } while (0);\n"
    "        buf++;\n"
    "    }\n"
    "    return buf;\n"
    "}\n"
    """
)

runtime_c = root / "tests/compiler/c0/core_discard_expression_m25_runtime.c"
runtime_c.write_text(
    """void core_m25_discard_pointer(unsigned int *value);\n"
    "void core_m25_discard_call(unsigned int *value);\n"
    "unsigned int *core_m25_le32_shape(unsigned int *buf, unsigned int words);\n\n"
    "int main(void) {\n"
    "    unsigned int value = 5U;\n"
    "    unsigned int words[5] = {0U, 0U, 0U, 0U, 0U};\n\n"
    "    core_m25_discard_pointer(&value);\n"
    "    if (value != 5U) {\n"
    "        return 1;\n"
    "    }\n"
    "    core_m25_discard_call(&value);\n"
    "    if (value != 8U) {\n"
    "        return 2;\n"
    "    }\n"
    "    if (core_m25_le32_shape(words, 3U) != &words[3]) {\n"
    "        return 3;\n"
    "    }\n"
    "    return 0;\n"
    "}\n"
    """
)

run_sh = root / "tests/compiler/c0/run-core-discard-expression-m25.sh"
run_sh.write_text(
    """#!/bin/sh\n"
    "set -eu\n"
    ": \"${MINIC:?set MINIC}\"\n"
    ": \"${RISCV_CC:=riscv64-linux-gnu-gcc}\"\n"
    ": \"${QEMU_RISCV64:=qemu-riscv64}\"\n"
    ": \"${BUILD_DIR:=build/core-discard-expression-m25}\"\n"
    "mkdir -p \"$BUILD_DIR\"\n"
    "MINIC_CORE_IR=strict \"$MINIC\" -S tests/compiler/c0/core_discard_expression_m25.c -o \"$BUILD_DIR/minic.s\"\n"
    "\"$RISCV_CC\" -O0 -static tests/compiler/c0/core_discard_expression_m25_runtime.c \"$BUILD_DIR/minic.s\" -o \"$BUILD_DIR/minic.elf\"\n"
    "\"$QEMU_RISCV64\" \"$BUILD_DIR/minic.elf\"\n"
    "\"$RISCV_CC\" -O0 -static tests/compiler/c0/core_discard_expression_m25_runtime.c tests/compiler/c0/core_discard_expression_m25.c -o \"$BUILD_DIR/gcc.elf\"\n"
    "\"$QEMU_RISCV64\" \"$BUILD_DIR/gcc.elf\"\n"
    "printf '%s\\n' 'PASS compiler/c0/core-discard-expression-m25'\n"
    """
)

gate_text = gate.read_text()
anchor = """core_postfix_update_m24_focused() {\n    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n    BUILD_DIR=\"$root/build/ci-core-postfix-update-m24\" \\\n    RISCV_CC=riscv64-linux-gnu-gcc \\\n    QEMU_RISCV64=qemu-riscv64 \\\n        sh tests/compiler/c0/run-core-postfix-update-m24.sh\n}\n\n"""
addition = anchor + """core_discard_expression_m25_focused() {\n    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n    BUILD_DIR=\"$root/build/ci-core-discard-expression-m25\" \\\n    RISCV_CC=riscv64-linux-gnu-gcc \\\n    QEMU_RISCV64=qemu-riscv64 \\\n        sh tests/compiler/c0/run-core-discard-expression-m25.sh\n}\n\n"""
gate_text = replace_once(gate_text, anchor, addition, "C0 focused function")
anchor = "start_gate core-postfix-update-m24-focused core_postfix_update_m24_focused\n"
addition = anchor + "start_gate core-discard-expression-m25-focused core_discard_expression_m25_focused\n"
gate_text = replace_once(gate_text, anchor, addition, "C0 start_gate")
gate.write_text(gate_text)
