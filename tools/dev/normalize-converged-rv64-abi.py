#!/usr/bin/env python3
from pathlib import Path
import re


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"{label}: anchor not found")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/target/riscv64/abi.h",
    """typedef enum MinicRiscv64AbiValueKind {
    MINIC_RISCV64_ABI_VALUE_IGNORE = 0,
    MINIC_RISCV64_ABI_VALUE_INTEGER,
    MINIC_RISCV64_ABI_VALUE_FLOAT,
    MINIC_RISCV64_ABI_VALUE_AGGREGATE,
    MINIC_RISCV64_ABI_VALUE_INDIRECT
} MinicRiscv64AbiValueKind;
""",
    """typedef enum MinicRiscv64AbiValueKind {
    MINIC_RISCV64_ABI_VALUE_INVALID = 0,
    MINIC_RISCV64_ABI_VALUE_VOID,
    MINIC_RISCV64_ABI_VALUE_IGNORE,
    MINIC_RISCV64_ABI_VALUE_INTEGER,
    MINIC_RISCV64_ABI_VALUE_FLOAT,
    MINIC_RISCV64_ABI_VALUE_AGGREGATE,
    MINIC_RISCV64_ABI_VALUE_INDIRECT
} MinicRiscv64AbiValueKind;
""",
    "ABI value-kind enum",
)

p = Path("src/target/riscv64/abi.c")
text = p.read_text()
text = text.replace('#include "target/riscv64/layout.h"\n', '#include "target/data_layout.h"\n', 1)
old = """bool minic_riscv64_classify_abi_value(const MinicC0Program *program,
                                      MinicType type,
                                      MinicRiscv64AbiValue *result) {
    size_t alignment;
    size_t size;

    if (program == NULL || result == NULL ||
        !minic_riscv64_type_layout(program, type, &size, &alignment)) {
        return false;
    }
    (void)alignment;

    result->storage_size = size;
    result->register_chunks = 0U;
"""
new = """bool minic_riscv64_classify_abi_value(const MinicC0Program *program,
                                      MinicType type,
                                      MinicRiscv64AbiValue *result) {
    size_t alignment;
    size_t size;

    if (program == NULL || result == NULL) {
        return false;
    }
    result->kind = MINIC_RISCV64_ABI_VALUE_INVALID;
    result->storage_size = 0U;
    result->register_chunks = 0U;
    result->slot_count = 0U;
    if (minic_type_is_void(type)) {
        result->kind = MINIC_RISCV64_ABI_VALUE_VOID;
        return true;
    }
    if (!minic_data_layout_type(
            minic_default_data_layout(), program, type, &size, &alignment)) {
        return false;
    }
    (void)alignment;

    result->storage_size = size;
"""
if new not in text:
    if old not in text:
        raise SystemExit("ABI classifier prologue: anchor not found")
    text = text.replace(old, new, 1)
else:
    text = text.replace(
        "if (!minic_riscv64_type_layout(program, type, &size, &alignment)) {",
        "if (!minic_data_layout_type(\n            minic_default_data_layout(), program, type, &size, &alignment)) {",
        1,
    )

old = """    if (value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
        value->slot_count = 0U;
    } else if (value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
"""
new = """    if (value->kind == MINIC_RISCV64_ABI_VALUE_VOID ||
        value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
        value->slot_count = 0U;
    } else if (value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
"""
if new not in text:
    if old not in text:
        raise SystemExit("ABI slot-count bridge: anchor not found")
    text = text.replace(old, new, 1)

old = """        cursor->integer_register_count > MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT ||
        cursor->floating_register_count > MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT ||
        !minic_riscv64_abi_classify_value(program, type, &result.value)) {
        return false;
    }
"""
new = """        cursor->integer_register_count > MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT ||
        cursor->floating_register_count > MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT ||
        !minic_riscv64_abi_classify_value(program, type, &result.value) ||
        result.value.kind == MINIC_RISCV64_ABI_VALUE_VOID) {
        return false;
    }
"""
if new not in text:
    if old not in text:
        raise SystemExit("ABI argument guard: anchor not found")
    text = text.replace(old, new, 1)
p.write_text(text)

# TargetABI is the sole aggregate classifier owner. Drop discovery's duplicate
# helper from codegen_support while retaining the public ABI adapter.
p = Path("src/target/riscv64/codegen_support.c")
text = p.read_text()
pattern = re.compile(
    r"static bool minic_riscv64_integer_aggregate_member_type\(.*?\n}\n\n(?=bool minic_riscv64_integer_aggregate_abi)",
    re.S,
)
text2, count = pattern.subn("", text, count=1)
if count not in (0, 1):
    raise SystemExit(f"duplicate aggregate classifier: matches={count}")
p.write_text(text2)

# Promote the expanded ABI semantics into the canonical unit test.
p = Path("tests/target/riscv64/abi_test.c")
text = p.read_text()
replace = {
    """    CHECK(!minic_riscv64_abi_classify_value(&program, record4, &value));
    CHECK(!minic_riscv64_abi_classify_value(&program, record_fp, &value));
""": """    CHECK(expect_value(&program, record4, MINIC_RISCV64_ABI_VALUE_AGGREGATE, 4U, 1U));
    CHECK(expect_value(&program, record_fp, MINIC_RISCV64_ABI_VALUE_INDIRECT, 8U, 1U));
""",
    """static bool test_unsupported_argument_is_transactional(void) {
    MinicC0Program program;
    MinicType field_type;
    MinicType record4;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiCursor before_failure;
    MinicRiscv64AbiArgumentLocation location;

    minic_c0_program_initialize(&program);
    field_type = minic_type_int();
    CHECK(add_record(&program, &field_type, 1U, &record4));

    minic_riscv64_abi_cursor_initialize(&cursor);
    CHECK(minic_riscv64_abi_place_argument(
        &program, minic_type_long(), true, &cursor, &location));
    before_failure = cursor;
    CHECK(!minic_riscv64_abi_place_argument(&program, record4, true, &cursor, &location));
""": """static bool test_unsupported_argument_is_transactional(void) {
    MinicC0Program program;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiCursor before_failure;
    MinicRiscv64AbiArgumentLocation location;

    minic_c0_program_initialize(&program);

    minic_riscv64_abi_cursor_initialize(&cursor);
    CHECK(minic_riscv64_abi_place_argument(
        &program, minic_type_long(), true, &cursor, &location));
    before_failure = cursor;
    CHECK(!minic_riscv64_abi_place_argument(
        &program, minic_type_void(), true, &cursor, &location));
""",
}
for old, new in replace.items():
    if new in text:
        continue
    if old not in text:
        raise SystemExit("ABI test migration anchor not found")
    text = text.replace(old, new, 1)
text = text.replace("    MinicRiscv64AbiValue value;\n", "", 1)
p.write_text(text)

print("NORMALIZED converged RV64 ABI contract")
