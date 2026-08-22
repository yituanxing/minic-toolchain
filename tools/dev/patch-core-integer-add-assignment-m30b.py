#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M30b {label} anchor count={count}, expected 1")
    return text.replace(old, new, 1)


path = Path('src/core/core_lower.c')
text = path.read_text()
text = replace_once(
    text,
    '''    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&
        (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND ||
''',
    '''    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND ||
''',
    'compound admission',
)
text = replace_once(
    text,
    '''        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_SUBTRACT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
            break;
        case MINIC_BINARY_BITWISE_AND:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
            break;
''',
    '''        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_ADD:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
            break;
        case MINIC_BINARY_SUBTRACT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
            break;
        case MINIC_BINARY_BITWISE_AND:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
            break;
''',
    'compound opcode',
)
path.write_text(text)
print('M30B_ADD_ASSIGNMENT_PATCH_APPLIED')
