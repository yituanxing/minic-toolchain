#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / 'src/target/riscv64/codegen_inline_asm.c'
text = path.read_text()

old = '''static bool constraint_is_immediate(const MinicInlineAsmOperand *operand) {
    return constraint_is(operand, "i") || constraint_is(operand, "I");
}
'''
new = '''static bool constraint_is_immediate(const MinicInlineAsmOperand *operand) {
    return constraint_is(operand, "i") || constraint_is(operand, "I");
}

static bool constraint_is_register_or_zero(const MinicInlineAsmOperand *operand) {
    return constraint_is(operand, "rJ");
}
'''
if text.count(old) != 1:
    raise SystemExit('constraint helper anchor missing')
text = text.replace(old, new, 1)

anchor = '''static const MinicGlobalObject *
inline_asm_symbolic_object_immediate(const MinicC0Program *program,
'''
helper = '''static bool inline_asm_operand_is_zero_immediate(const MinicC0Program *program,
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
if text.count(anchor) != 1:
    raise SystemExit('symbolic immediate helper anchor missing')
text = text.replace(anchor, helper + anchor, 1)

old_validate = '''    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&
               !constraint_is(operand, "i")) {
        return false;
    }
'''
new_validate = '''    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&
               !constraint_is(operand, "i") && !constraint_is_register_or_zero(operand)) {
        return false;
    }
'''
if text.count(old_validate) != 1:
    raise SystemExit('input constraint validation anchor missing')
text = text.replace(old_validate, new_validate, 1)

old_assign_sig = '''static bool assign_operand_registers(const MinicInlineAsm *inline_asm,
                                     const char **operand_registers,
                                     size_t operand_count) {'''
new_assign_sig = '''static bool assign_operand_registers(const MinicInlineAsm *inline_asm,
                                     const MinicC0Program *program,
                                     const char **operand_registers,
                                     size_t operand_count) {'''
if text.count(old_assign_sig) != 1:
    raise SystemExit('register assignment signature anchor missing')
text = text.replace(old_assign_sig, new_assign_sig, 1)
text = text.replace('''    if (inline_asm == NULL || operand_registers == NULL) {
        return false;
    }
''', '''    if (inline_asm == NULL || program == NULL || operand_registers == NULL) {
        return false;
    }
''', 1)
old_assign_immediate = '''        if (constraint_is_immediate(operand)) {
            operand_registers[operand_index] = NULL;
            continue;
        }
'''
new_assign_immediate = '''        if (inline_asm_operand_uses_immediate(program, operand)) {
            operand_registers[operand_index] = NULL;
            continue;
        }
'''
if text.count(old_assign_immediate) != 1:
    raise SystemExit('register assignment immediate anchor missing')
text = text.replace(old_assign_immediate, new_assign_immediate, 1)

old_resolve_sig = '''static bool resolve_template_reference(const MinicInlineAsm *inline_asm,
                                       size_t operand_count,
                                       size_t *template_index,
                                       size_t *operand_index,
                                       bool *literal_percent) {'''
new_resolve_sig = '''static bool resolve_template_reference(const MinicInlineAsm *inline_asm,
                                       size_t operand_count,
                                       size_t *template_index,
                                       size_t *operand_index,
                                       bool *literal_percent,
                                       char *modifier) {'''
if text.count(old_resolve_sig) != 1:
    raise SystemExit('template resolver signature anchor missing')
text = text.replace(old_resolve_sig, new_resolve_sig, 1)
old_resolve_guard = '''    if (inline_asm == NULL || template_index == NULL || operand_index == NULL ||
        literal_percent == NULL || *template_index >= inline_asm->template_length ||
        inline_asm->template_text[*template_index] != '%') {
        return false;
    }
'''
new_resolve_guard = '''    if (inline_asm == NULL || template_index == NULL || operand_index == NULL ||
        literal_percent == NULL || modifier == NULL ||
        *template_index >= inline_asm->template_length ||
        inline_asm->template_text[*template_index] != '%') {
        return false;
    }
'''
if text.count(old_resolve_guard) != 1:
    raise SystemExit('template resolver guard anchor missing')
text = text.replace(old_resolve_guard, new_resolve_guard, 1)
old_percent = '''    if (ch == '%') {
        *template_index = index;
        *literal_percent = true;
        *operand_index = 0U;
        return true;
    }
    *literal_percent = false;
'''
new_percent = '''    *modifier = '\\0';
    if (ch == '%') {
        *template_index = index;
        *literal_percent = true;
        *operand_index = 0U;
        return true;
    }
    *literal_percent = false;
    if (ch == 'z') {
        *modifier = 'z';
        index += 1U;
        if (index >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[index];
    }
'''
if text.count(old_percent) != 1:
    raise SystemExit('template modifier anchor missing')
text = text.replace(old_percent, new_percent, 1)

old_local = '''        size_t operand_index;
        bool literal_percent;
'''
new_local = '''        size_t operand_index;
        bool literal_percent;
        char modifier;
'''
if text.count(old_local) < 2:
    raise SystemExit('template local anchors missing')
text = text.replace(old_local, new_local, 2)
old_call = '''        if (!resolve_template_reference(
                inline_asm, operand_count, &index, &operand_index, &literal_percent)) {
            return false;
        }
'''
new_call = '''        if (!resolve_template_reference(inline_asm,
                                        operand_count,
                                        &index,
                                        &operand_index,
                                        &literal_percent,
                                        &modifier)) {
            return false;
        }
'''
if text.count(old_call) != 2:
    raise SystemExit(f'template resolver call anchors={text.count(old_call)}')
text = text.replace(old_call, new_call, 2)
old_voids = '''        (void)operand_index;
        (void)literal_percent;
'''
new_voids = '''        (void)operand_index;
        (void)literal_percent;
        if (modifier != '\\0' && modifier != 'z') {
            return false;
        }
'''
if text.count(old_voids) != 1:
    raise SystemExit('template validation modifier anchor missing')
text = text.replace(old_voids, new_voids, 1)

old_emit = '''            register_name = operand_registers[operand_index];
            if (constraint_is_immediate(operand)) {
                if (!emit_immediate_operand(file, program, operand, inline_asm_id, operand_index)) {
                    return false;
                }
            } else if (register_name == NULL) {
'''
new_emit = '''            register_name = operand_registers[operand_index];
            if (modifier == 'z') {
                int64_t immediate_value;

                if (inline_asm_integer_immediate_value(
                        program, operand->expression, &immediate_value) &&
                    immediate_value == 0) {
                    if (fputs("zero", file) == EOF) {
                        return false;
                    }
                    continue;
                }
            }
            if (inline_asm_operand_uses_immediate(program, operand)) {
                if (!emit_immediate_operand(file, program, operand, inline_asm_id, operand_index)) {
                    return false;
                }
            } else if (register_name == NULL) {
'''
if text.count(old_emit) != 1:
    raise SystemExit('template emission branch anchor missing')
text = text.replace(old_emit, new_emit, 1)

old_assign_call = '''    if (!assign_operand_registers(inline_asm, operand_registers, operand_count)) {
        return false;
    }
'''
new_assign_call = '''    if (!assign_operand_registers(inline_asm, program, operand_registers, operand_count)) {
        return false;
    }
'''
if text.count(old_assign_call) != 1:
    raise SystemExit('register assignment call anchor missing')
text = text.replace(old_assign_call, new_assign_call, 1)

old_stage = '''        if (constraint_is_immediate(operand)) {
            continue;
        }
'''
new_stage = '''        if (inline_asm_operand_uses_immediate(program, operand)) {
            continue;
        }
'''
if text.count(old_stage) != 1:
    raise SystemExit(f'input staging anchor count={text.count(old_stage)}')
text = text.replace(old_stage, new_stage, 1)

old_reload = '''        if (constraint_is_immediate(&inline_asm->inputs[index])) {
            continue;
        }
'''
new_reload = '''        if (inline_asm_operand_uses_immediate(program, &inline_asm->inputs[index])) {
            continue;
        }
'''
if text.count(old_reload) != 1:
    raise SystemExit(f'input reload anchor count={text.count(old_reload)}')
text = text.replace(old_reload, new_reload, 1)

path.write_text(text)

source_path = root / 'tests/compiler/c0/gnu_inline_asm_operands.c'
source = source_path.read_text()
marker = '\nint main(void) {'
insert = '''\nstatic int linux_rj_runtime_shape(int value) {\n    int result;\n\n    __asm__ __volatile__("add %0, zero, %z1" : "=r"(result) : "rJ"(value));\n    return result;\n}\n\nstatic int linux_rj_zero_shape(void) {\n    int result;\n\n    __asm__ __volatile__("add %0, zero, %z1" : "=r"(result) : "rJ"(0));\n    return result;\n}\n'''
if source.count(marker) != 1:
    raise SystemExit('inline asm rJ source marker missing')
source_path.write_text(source.replace(marker, insert + marker, 1))

run_path = root / 'tests/compiler/c0/run-gnu-inline-asm-operands.sh'
run = run_path.read_text()
needle = '''grep -F '.org 2b + 4' "$assembly" >/dev/null\n'''
extra = needle + '''grep -F 'add t0, zero, t1' "$assembly" >/dev/null\ngrep -F 'add t0, zero, zero' "$assembly" >/dev/null\n'''
if run.count(needle) != 1:
    raise SystemExit('inline asm rJ assertion anchor missing')
run = run.replace(needle, extra, 1)
old_msg = "'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=r,+r inputs=r,I,i clobber=memory,t3 symbolic-i=global-string const-i=typed-consteval staging=stack target=RV64'"
new_msg = "'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=r,+r inputs=r,I,i,rJ modifiers=z clobber=memory,t3 symbolic-i=global-string rJ=runtime+zero target=RV64'"
if run.count(old_msg) != 1:
    raise SystemExit('inline asm rJ message anchor missing')
run_path.write_text(run.replace(old_msg, new_msg, 1))
