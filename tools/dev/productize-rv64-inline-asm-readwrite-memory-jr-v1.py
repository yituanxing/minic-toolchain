#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count == 0 and new in text:
        return
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


path = "src/target/riscv64/codegen_inline_asm.c"

replace_once(
    path,
    '''    if (constraint_is(operand, "=m")) {
        return operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY;
    }
''',
    '''    if (constraint_is(operand, "=m")) {
        return operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY;
    }
    if (constraint_is(operand, "+m")) {
        return operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE;
    }
''',
    "readwrite-memory-validation",
)

replace_once(
    path,
    '''    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&
               !constraint_is(operand, "i") && !constraint_is(operand, "rJ") &&
               !constraint_is(operand, "rK") && !constraint_is(operand, "m")) {
''',
    '''    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&
               !constraint_is(operand, "i") && !constraint_is(operand, "rJ") &&
               !constraint_is(operand, "Jr") && !constraint_is(operand, "rK") &&
               !constraint_is(operand, "m")) {
''',
    "jr-input-validation",
)

replace_once(
    path,
    '''    if (constraint_is(operand, "rJ")) {
        return minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type);
    }
''',
    '''    if (constraint_is(operand, "rJ") || constraint_is(operand, "Jr")) {
        return minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type);
    }
''',
    "jr-input-type",
)

replace_once(
    path,
    '''    if (constraint_is(operand, "rJ")) {
        return operand_constant_zero(program, operand);
    }
''',
    '''    if (constraint_is(operand, "rJ") || constraint_is(operand, "Jr")) {
        return operand_constant_zero(program, operand);
    }
''',
    "jr-immediate-choice",
)

replace_once(
    path,
    '''    return constraint_is(operand, "r") || constraint_is(operand, "rJ") ||
           constraint_is(operand, "rK") || constraint_is(operand, "=r") ||
''',
    '''    return constraint_is(operand, "r") || constraint_is(operand, "rJ") ||
           constraint_is(operand, "Jr") || constraint_is(operand, "rK") ||
           constraint_is(operand, "=r") ||
''',
    "jr-register-choice",
)

replace_once(
    path,
    '''    if (constraint_is(operand, "rJ") && operand_constant_zero(program, operand)) {
        return fputc('0', file) != EOF;
    }
''',
    '''    if ((constraint_is(operand, "rJ") || constraint_is(operand, "Jr")) &&
        operand_constant_zero(program, operand)) {
        return fputc('0', file) != EOF;
    }
''',
    "jr-zero-render",
)

replace_once(
    path,
    '''            } else if (constraint_is(operand, "=m") || constraint_is(operand, "m")) {
''',
    '''            } else if (constraint_is(operand, "=m") || constraint_is(operand, "+m") ||
                       constraint_is(operand, "m")) {
''',
    "readwrite-memory-template",
)

replace_once(
    path,
    '''        if (constraint_is(operand, "+A") || constraint_is(operand, "=m") ||
            constraint_is(operand, "=r") || constraint_is(operand, "=&r")) {
''',
    '''        if (constraint_is(operand, "+A") || constraint_is(operand, "=m") ||
            constraint_is(operand, "+m") || constraint_is(operand, "=r") ||
            constraint_is(operand, "=&r")) {
''',
    "readwrite-memory-address-save",
)

replace_once(
    path,
    '''        if ((constraint_is(&inline_asm->outputs[index], "+A") ||
             constraint_is(&inline_asm->outputs[index], "=m") ||
             constraint_is_readwrite_register(&inline_asm->outputs[index])) &&
''',
    '''        if ((constraint_is(&inline_asm->outputs[index], "+A") ||
             constraint_is(&inline_asm->outputs[index], "=m") ||
             constraint_is(&inline_asm->outputs[index], "+m") ||
             constraint_is_readwrite_register(&inline_asm->outputs[index])) &&
''',
    "readwrite-memory-address-load",
)
