#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M43 {label}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/core/core_lower.c",
    '''        if (operand->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_integer(operand->type) || minic_type_is_bool_integer(operand->type) ||
            minic_type_is_const(operand->type) || minic_type_is_volatile(operand->type) ||
            !minic_type_unqualified(operand->type, &value_type) ||
''',
    '''        if (operand->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_integer(operand->type) || minic_type_is_bool_integer(operand->type) ||
            minic_type_is_const(operand->type) ||
            !minic_type_unqualified(operand->type, &value_type) ||
''',
    "allow volatile prefix integer lvalues",
)

replace_once(
    "src/core/core_lower.c",
    '''        update_instruction.value.load.address = address_value;
        update_instruction.value.load.is_volatile = false;
        if (!minic_core_function_append_value_instruction(
''',
    '''        update_instruction.value.load.address = address_value;
        update_instruction.value.load.is_volatile = minic_type_is_volatile(operand->type);
        if (!minic_core_function_append_value_instruction(
''',
    "volatile prefix load",
)

replace_once(
    "src/core/core_lower.c",
    '''        update_instruction.value.store.address = address_value;
        update_instruction.value.store.stored_value = updated_value;
        update_instruction.value.store.is_volatile = false;
        if (!minic_core_function_append_effect_instruction(
''',
    '''        update_instruction.value.store.address = address_value;
        update_instruction.value.store.stored_value = updated_value;
        update_instruction.value.store.is_volatile = minic_type_is_volatile(operand->type);
        if (!minic_core_function_append_effect_instruction(
''',
    "volatile prefix store",
)

print("M43_PATCH_APPLIED")
