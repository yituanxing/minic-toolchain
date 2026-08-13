#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_inline_asm.c")
text = path.read_text()

old_validate = '''    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&
               !constraint_is(operand, "i") && !constraint_is(operand, "rJ")) {
        return false;
    }
'''
new_validate = '''    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&
               !constraint_is(operand, "i") && !constraint_is(operand, "rJ") &&
               !constraint_is(operand, "rK")) {
        return false;
    }
'''
if old_validate in text:
    text = text.replace(old_validate, new_validate, 1)
elif new_validate not in text:
    raise SystemExit("rK validation anchor not found")

old_helper = '''static bool operand_uses_immediate(const MinicC0Program *program,
                                   const MinicInlineAsmOperand *operand) {
    if (constraint_is_immediate(operand)) {
        return true;
    }
    return constraint_is(operand, "rJ") && operand_constant_zero(program, operand);
}
'''
new_helper = '''static bool operand_constant_u5(const MinicC0Program *program,
                                const MinicInlineAsmOperand *operand) {
    MinicConstValue constant;
    int64_t value;

    return program != NULL && operand != NULL &&
           minic_const_eval_integer(
               program, minic_default_target_info(), operand->expression, &constant) &&
           minic_const_value_as_int64(
               program, minic_default_target_info(), &constant, &value) &&
           value >= 0 && value <= 31;
}

static bool operand_uses_immediate(const MinicC0Program *program,
                                   const MinicInlineAsmOperand *operand) {
    if (constraint_is_immediate(operand)) {
        return true;
    }
    if (constraint_is(operand, "rJ")) {
        return operand_constant_zero(program, operand);
    }
    return constraint_is(operand, "rK") && operand_constant_u5(program, operand);
}
'''
if old_helper in text:
    text = text.replace(old_helper, new_helper, 1)
elif new_helper not in text:
    raise SystemExit("rK operand choice anchor not found")

path.write_text(text)
