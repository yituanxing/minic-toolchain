#!/usr/bin/env python3
from pathlib import Path

p = Path('src/target/riscv64/core_codegen.c')
text = p.read_text()
old = '''    return operand->access == MINIC_CORE_INLINE_ASM_OPERAND_READ_ONLY &&
           core_inline_asm_constraint_is(operand, "r") &&
           (minic_type_is_integer(function->values[operand->value].type) ||
            minic_type_is_pointer(function->values[operand->value].type));
'''
new = '''    return operand->access == MINIC_CORE_INLINE_ASM_OPERAND_READ_ONLY &&
           (core_inline_asm_constraint_is(operand, "r") ||
            core_inline_asm_constraint_is(operand, "rK") ||
            core_inline_asm_constraint_is(operand, "rJ") ||
            core_inline_asm_constraint_is(operand, "Jr")) &&
           (minic_type_is_integer(function->values[operand->value].type) ||
            minic_type_is_pointer(function->values[operand->value].type));
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f'expected one Core asm input support anchor, found {count}')
p.write_text(text.replace(old, new, 1))
print('M32_PATCH_APPLIED')
