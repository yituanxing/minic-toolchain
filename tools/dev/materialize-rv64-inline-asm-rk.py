#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / 'src/target/riscv64/codegen_inline_asm.c'
text = path.read_text()

old = '''static bool constraint_is_register_or_zero(const MinicInlineAsmOperand *operand) {
    return constraint_is(operand, "rJ");
}
'''
new = '''static bool constraint_is_register_or_target_immediate(const MinicInlineAsmOperand *operand) {
    return constraint_is(operand, "rJ") || constraint_is(operand, "rK");
}
'''
if text.count(old) != 1:
    raise SystemExit('rJ constraint helper anchor missing')
text = text.replace(old, new, 1)

old_zero = '''static bool inline_asm_operand_is_zero_immediate(const MinicC0Program *program,
                                                 const MinicInlineAsmOperand *operand) {
    int64_t value;

    return program != NULL && operand != NULL && constraint_is_register_or_zero(operand) &&
           inline_asm_integer_immediate_value(program, operand->expression, &value) && value == 0;
}

static bool inline_asm_operand_uses_immediate(const MinicC0Program *program,
                                              const MinicInlineAsmOperand *operand) {
    return constraint_is_immediate(operand) ||
           inline_asm_operand_is_zero_immediate(program, operand);
}
'''
new_zero = '''static bool inline_asm_operand_target_immediate(const MinicC0Program *program,
                                                const MinicInlineAsmOperand *operand,
                                                int64_t *value) {
    int64_t parsed;

    if (program == NULL || operand == NULL || value == NULL ||
        !constraint_is_register_or_target_immediate(operand) ||
        !inline_asm_integer_immediate_value(program, operand->expression, &parsed)) {
        return false;
    }
    if (constraint_is(operand, "rJ")) {
        if (parsed != 0) {
            return false;
        }
    } else if (constraint_is(operand, "rK")) {
        if (parsed < 0 || parsed > 31) {
            return false;
        }
    } else {
        return false;
    }
    *value = parsed;
    return true;
}

static bool inline_asm_operand_uses_immediate(const MinicC0Program *program,
                                              const MinicInlineAsmOperand *operand) {
    int64_t value;

    return constraint_is_immediate(operand) ||
           inline_asm_operand_target_immediate(program, operand, &value);
}
'''
if text.count(old_zero) != 1:
    raise SystemExit('rJ immediate selector anchor missing')
text = text.replace(old_zero, new_zero, 1)

old_validate = '''               !constraint_is(operand, "i") && !constraint_is_register_or_zero(operand)) {
'''
new_validate = '''               !constraint_is(operand, "i") &&
               !constraint_is_register_or_target_immediate(operand)) {
'''
if text.count(old_validate) != 1:
    raise SystemExit('rJ validation anchor missing')
text = text.replace(old_validate, new_validate, 1)

path.write_text(text)

source_path = root / 'tests/compiler/c0/gnu_inline_asm_operands.c'
source = source_path.read_text()
marker = '\nint main(void) {'
insert = '''\nstatic void linux_rk_immediate_shape(void) {\n    unsigned long value = 2UL;\n\n    __asm__ __volatile__("csrs 0x100, %0" : : "rK"(2UL) : "memory");\n    __asm__ __volatile__("csrs 0x100, %0" : : "rK"(value) : "memory");\n}\n'''
if source.count(marker) != 1:
    raise SystemExit('rK source marker missing')
source_path.write_text(source.replace(marker, insert + marker, 1))

run_path = root / 'tests/compiler/c0/run-gnu-inline-asm-operands.sh'
run = run_path.read_text()
needle = '''grep -F 'add t0, zero, zero' "$assembly" >/dev/null\n'''
extra = needle + '''grep -F 'csrs 0x100, 2' "$assembly" >/dev/null\ngrep -F 'csrs 0x100, t0' "$assembly" >/dev/null\n'''
if run.count(needle) != 1:
    raise SystemExit('rK assertion marker missing')
run = run.replace(needle, extra, 1)
old_msg = "'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=r,+r inputs=r,I,i,rJ modifiers=z clobber=memory,t3 symbolic-i=global-string rJ=runtime+zero target=RV64'"
new_msg = "'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=r,+r inputs=r,I,i,rJ,rK modifiers=z clobber=memory,t3 alternatives=rJ-zero+rK-u5 target=RV64'"
if run.count(old_msg) != 1:
    raise SystemExit('rK message anchor missing')
run_path.write_text(run.replace(old_msg, new_msg, 1))
