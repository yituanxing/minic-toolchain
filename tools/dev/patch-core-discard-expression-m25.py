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
anchor = """    if (expression->value_category != MINIC_VALUE_RVALUE) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
"""
replacement = """    if (expression->value_category != MINIC_VALUE_RVALUE) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_DISCARD) {
        const MinicExpression *operand;
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        if (!minic_type_is_void(expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        operand = minic_c0_program_expression(context->body->program,
                                              expression->value.unary.operand);
        if (operand == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_expression(context, expression->value.unary.operand, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (minic_type_is_void(operand->type)) {
            if (discarded_value != MINIC_CORE_VALUE_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
        } else if (discarded_value == MINIC_CORE_VALUE_INVALID ||
                   discarded_value >= context->function->value_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = MINIC_CORE_VALUE_INVALID;
        return MINIC_CORE_LOWER_OK;
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
"""
text = replace_once(text, anchor, replacement, "lower_expression discard")

anchor = """    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
        MinicCoreValueId discarded_value;

        return lower_expression(context, statement->expression, &discarded_value);
    }
"""
replacement = anchor + """    if (expression->kind == MINIC_EXPRESSION_DISCARD) {
        MinicCoreValueId discarded_value;

        return lower_expression(context, statement->expression, &discarded_value);
    }
"""
text = replace_once(text, anchor, replacement, "expression-statement discard")
core.write_text(text)

focused_c = root / "tests/compiler/c0/core_discard_expression_m25.c"
focused_c.write_text("""static unsigned int core_m25_touch(unsigned int *value) {
    *value = *value + 3U;
    return *value;
}

void core_m25_discard_pointer(unsigned int *value) {
    (void)(value);
}

void core_m25_discard_call(unsigned int *value) {
    (void)core_m25_touch(value);
}

unsigned int *core_m25_le32_shape(unsigned int *buf, unsigned int words) {
    while (words--) {
        do {
            (void)(buf);
        } while (0);
        buf++;
    }
    return buf;
}
""")

runtime_c = root / "tests/compiler/c0/core_discard_expression_m25_runtime.c"
runtime_c.write_text("""void core_m25_discard_pointer(unsigned int *value);
void core_m25_discard_call(unsigned int *value);
unsigned int *core_m25_le32_shape(unsigned int *buf, unsigned int words);

int main(void) {
    unsigned int value = 5U;
    unsigned int words[5] = {0U, 0U, 0U, 0U, 0U};

    core_m25_discard_pointer(&value);
    if (value != 5U) {
        return 1;
    }
    core_m25_discard_call(&value);
    if (value != 8U) {
        return 2;
    }
    if (core_m25_le32_shape(words, 3U) != &words[3]) {
        return 3;
    }
    return 0;
}
""")

run_sh = root / "tests/compiler/c0/run-core-discard-expression-m25.sh"
run_sh.write_text("""#!/bin/sh
set -eu
: "${MINIC:?set MINIC}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${BUILD_DIR:=build/core-discard-expression-m25}"
mkdir -p "$BUILD_DIR"
MINIC_CORE_IR=strict "$MINIC" -S tests/compiler/c0/core_discard_expression_m25.c -o "$BUILD_DIR/minic.s"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_discard_expression_m25_runtime.c "$BUILD_DIR/minic.s" -o "$BUILD_DIR/minic.elf"
"$QEMU_RISCV64" "$BUILD_DIR/minic.elf"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_discard_expression_m25_runtime.c tests/compiler/c0/core_discard_expression_m25.c -o "$BUILD_DIR/gcc.elf"
"$QEMU_RISCV64" "$BUILD_DIR/gcc.elf"
printf '%s\n' 'PASS compiler/c0/core-discard-expression-m25'
""")

gate_text = gate.read_text()
anchor = """core_postfix_update_m24_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-postfix-update-m24" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-postfix-update-m24.sh
}

"""
addition = anchor + """core_discard_expression_m25_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-discard-expression-m25" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-discard-expression-m25.sh
}

"""
gate_text = replace_once(gate_text, anchor, addition, "C0 focused function")
anchor = "start_gate core-postfix-update-m24-focused core_postfix_update_m24_focused\n"
addition = anchor + "start_gate core-discard-expression-m25-focused core_discard_expression_m25_focused\n"
gate_text = replace_once(gate_text, anchor, addition, "C0 start_gate")
gate.write_text(gate_text)
