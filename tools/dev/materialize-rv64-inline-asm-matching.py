#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / 'src/target/riscv64/codegen_inline_asm.c'
text = path.read_text()

anchor = '''static bool constraint_is_register_or_target_immediate(const MinicInlineAsmOperand *operand) {
    return constraint_is(operand, "rJ") || constraint_is(operand, "rK");
}
'''
helper = anchor + '''
static bool matching_output_index(const MinicInlineAsm *inline_asm,
                                  const MinicInlineAsmOperand *operand,
                                  size_t *output_index) {
    size_t index;
    size_t value;

    if (inline_asm == NULL || operand == NULL || output_index == NULL ||
        operand->constraint_text == NULL || operand->constraint_length == 0U) {
        return false;
    }
    value = 0U;
    for (index = 0U; index < operand->constraint_length; ++index) {
        unsigned char ch;
        size_t digit;

        ch = (unsigned char)operand->constraint_text[index];
        if (ch < '0' || ch > '9') {
            return false;
        }
        digit = (size_t)(ch - '0');
        if (value > (SIZE_MAX - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
    }
    if (value >= inline_asm->output_count) {
        return false;
    }
    if (!constraint_is(&inline_asm->outputs[value], "=r") &&
        !constraint_is(&inline_asm->outputs[value], "=&r") &&
        !constraint_is(&inline_asm->outputs[value], "+r")) {
        return false;
    }
    *output_index = value;
    return true;
}
'''
if text.count(anchor) != 1:
    raise SystemExit('constraint family anchor missing')
text = text.replace(anchor, helper, 1)

old_validate = '''    if (inline_asm->is_goto) {
        if (!constraint_is(operand, "i")) {
            return false;
        }
    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&
               !constraint_is(operand, "i") &&
               !constraint_is_register_or_target_immediate(operand)) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
'''
new_validate = '''    if (inline_asm->is_goto) {
        if (!constraint_is(operand, "i")) {
            return false;
        }
    } else {
        size_t matched_output;

        if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&
            !constraint_is(operand, "i") &&
            !constraint_is_register_or_target_immediate(operand) &&
            !matching_output_index(inline_asm, operand, &matched_output)) {
            return false;
        }
    }
    expression = minic_c0_program_expression(program, operand->expression);
'''
if text.count(old_validate) != 1:
    raise SystemExit('input validation anchor missing')
text = text.replace(old_validate, new_validate, 1)

old_assign = '''        if (inline_asm_operand_uses_immediate(program, operand)) {
            operand_registers[operand_index] = NULL;
            continue;
        }
        while (candidate_index < MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS &&
'''
new_assign = '''        if (inline_asm_operand_uses_immediate(program, operand)) {
            operand_registers[operand_index] = NULL;
            continue;
        }
        if (operand_index >= inline_asm->output_count) {
            size_t matched_output;

            if (matching_output_index(inline_asm, operand, &matched_output)) {
                if (operand_registers[matched_output] == NULL) {
                    return false;
                }
                operand_registers[operand_index] = operand_registers[matched_output];
                continue;
            }
        }
        while (candidate_index < MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS &&
'''
if text.count(old_assign) != 1:
    raise SystemExit('register assignment anchor missing')
text = text.replace(old_assign, new_assign, 1)

old_stage = '''        operand = &inline_asm->inputs[index];
        operand_index = inline_asm->output_count + index;
        if (inline_asm_operand_uses_immediate(program, operand)) {
            continue;
        }
        if (!minic_riscv64_emit_expression(file, program, function, operand->expression) ||
            !minic_riscv64_emit_sp_store64(file, "a0", operand_index * 8U)) {
            return false;
        }
'''
new_stage = '''        operand = &inline_asm->inputs[index];
        operand_index = inline_asm->output_count + index;
        if (inline_asm_operand_uses_immediate(program, operand)) {
            continue;
        }
        {
            size_t storage_index;
            size_t matched_output;

            storage_index = operand_index;
            if (matching_output_index(inline_asm, operand, &matched_output)) {
                storage_index = matched_output;
            }
            if (!minic_riscv64_emit_expression(file, program, function, operand->expression) ||
                !minic_riscv64_emit_sp_store64(file, "a0", storage_index * 8U)) {
                return false;
            }
        }
'''
if text.count(old_stage) != 1:
    raise SystemExit('input staging anchor missing')
text = text.replace(old_stage, new_stage, 1)

old_reload = '''        operand_index = inline_asm->output_count + index;
        if (inline_asm_operand_uses_immediate(program, &inline_asm->inputs[index])) {
            continue;
        }
        if (!minic_riscv64_emit_sp_load64(
                file, operand_registers[operand_index], operand_index * 8U)) {
            return false;
        }
'''
new_reload = '''        operand_index = inline_asm->output_count + index;
        if (inline_asm_operand_uses_immediate(program, &inline_asm->inputs[index])) {
            continue;
        }
        {
            size_t storage_index;
            size_t matched_output;

            storage_index = operand_index;
            if (matching_output_index(inline_asm, &inline_asm->inputs[index], &matched_output)) {
                storage_index = matched_output;
            }
            if (!minic_riscv64_emit_sp_load64(
                    file, operand_registers[operand_index], storage_index * 8U)) {
                return false;
            }
        }
'''
if text.count(old_reload) != 1:
    raise SystemExit('input reload anchor missing')
text = text.replace(old_reload, new_reload, 1)

path.write_text(text)

source_path = root / 'tests/compiler/c0/gnu_inline_asm_operands.c'
source = source_path.read_text()
marker = '\nint main(void) {'
insert = '''\nstatic unsigned long linux_matching_constraint_shape(void *pointer) {\n    unsigned long result;\n\n    __asm__("" : "=r"(result) : "0"(pointer));\n    return result;\n}\n'''
if source.count(marker) != 1:
    raise SystemExit('matching constraint source marker missing')
source_path.write_text(source.replace(marker, insert + marker, 1))

run_path = root / 'tests/compiler/c0/run-gnu-inline-asm-operands.sh'
run = run_path.read_text()
needle = '''grep -F 'csrs 0x100, t0' "$assembly" >/dev/null\n'''
extra = needle + '''matching_block=$(sed -n '/linux_matching_constraint_shape:/,/\\.size linux_matching_constraint_shape/p' "$assembly")\nprintf '%s\\n' "$matching_block" | grep -F 'ld t0, 0(sp)' >/dev/null\nif printf '%s\\n' "$matching_block" | grep -F 'ld t1,' >/dev/null; then\n    echo 'FAIL compiler/c0/gnu_inline_asm_operands matching constraint allocated a second register' >&2\n    exit 1\nfi\n'''
if run.count(needle) != 1:
    raise SystemExit('matching constraint assertion marker missing')
run = run.replace(needle, extra, 1)
old_msg = "'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=r,+r inputs=r,I,i,rJ,rK modifiers=z clobber=memory,t3 alternatives=rJ-zero+rK-u5 target=RV64'"
new_msg = "'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=r,+r inputs=r,I,i,rJ,rK,matching modifiers=z matching=same-register alternatives=rJ-zero+rK-u5 target=RV64'"
if run.count(old_msg) != 1:
    raise SystemExit('matching constraint message anchor missing')
run_path.write_text(run.replace(old_msg, new_msg, 1))
