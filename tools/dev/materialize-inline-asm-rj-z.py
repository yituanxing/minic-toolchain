#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_inline_asm.c")
text = path.read_text()

old_validate = '''    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&
               !constraint_is(operand, "i")) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    return expression != NULL &&
           (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
}
'''
new_validate = '''    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&
               !constraint_is(operand, "i") && !constraint_is(operand, "rJ")) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    if (expression == NULL) {
        return false;
    }
    if (constraint_is(operand, "rJ")) {
        return minic_type_is_integer(expression->type);
    }
    return minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type);
}
'''
if old_validate in text:
    text = text.replace(old_validate, new_validate, 1)
elif new_validate not in text:
    raise SystemExit("rJ validation anchor not found")

marker = '''static bool inline_asm_clobbers_register(const MinicInlineAsm *inline_asm,
'''
helper = '''static bool operand_constant_zero(const MinicC0Program *program,
                                  const MinicInlineAsmOperand *operand) {
    MinicConstValue constant;
    bool is_zero;

    return program != NULL && operand != NULL &&
           minic_const_eval_integer(
               program, minic_default_target_info(), operand->expression, &constant) &&
           minic_const_value_is_zero(
               program, minic_default_target_info(), &constant, &is_zero) &&
           is_zero;
}

static bool operand_uses_immediate(const MinicC0Program *program,
                                   const MinicInlineAsmOperand *operand) {
    if (constraint_is_immediate(operand)) {
        return true;
    }
    return constraint_is(operand, "rJ") && operand_constant_zero(program, operand);
}

'''
if helper not in text:
    if marker not in text:
        raise SystemExit("operand choice helper anchor not found")
    text = text.replace(marker, helper + marker, 1)

old_assign_sig = '''static bool assign_operand_registers(const MinicInlineAsm *inline_asm,
                                     const char **operand_registers,
                                     size_t operand_count) {
'''
new_assign_sig = '''static bool assign_operand_registers(const MinicInlineAsm *inline_asm,
                                     const MinicC0Program *program,
                                     const char **operand_registers,
                                     size_t operand_count) {
'''
if old_assign_sig in text:
    text = text.replace(old_assign_sig, new_assign_sig, 1)
elif new_assign_sig not in text:
    raise SystemExit("assign signature anchor not found")
text = text.replace('''    if (inline_asm == NULL || operand_registers == NULL) {
''', '''    if (inline_asm == NULL || program == NULL || operand_registers == NULL) {
''', 1)
text = text.replace('''        if (constraint_is_immediate(operand)) {
            operand_registers[operand_index] = NULL;
            continue;
        }
''', '''        if (operand_uses_immediate(program, operand)) {
            operand_registers[operand_index] = NULL;
            continue;
        }
''', 1)
text = text.replace('''        !assign_operand_registers(inline_asm, operand_registers, operand_count)) {
''', '''        !assign_operand_registers(inline_asm, program, operand_registers, operand_count)) {
''', 1)

old_resolve_sig = '''static bool resolve_template_reference(const MinicInlineAsm *inline_asm,
                                       size_t operand_count,
                                       size_t *template_index,
                                       size_t *operand_index,
                                       bool *literal_percent) {
'''
new_resolve_sig = '''static bool resolve_template_reference(const MinicInlineAsm *inline_asm,
                                       size_t operand_count,
                                       size_t *template_index,
                                       size_t *operand_index,
                                       bool *literal_percent,
                                       bool *zero_modifier) {
'''
if old_resolve_sig in text:
    text = text.replace(old_resolve_sig, new_resolve_sig, 1)
elif new_resolve_sig not in text:
    raise SystemExit("template resolver signature anchor not found")
text = text.replace('''    if (inline_asm == NULL || template_index == NULL || operand_index == NULL ||
        literal_percent == NULL || *template_index >= inline_asm->template_length ||
''', '''    if (inline_asm == NULL || template_index == NULL || operand_index == NULL ||
        literal_percent == NULL || zero_modifier == NULL ||
        *template_index >= inline_asm->template_length ||
''', 1)
text = text.replace('''    *literal_percent = false;
    if (ch >= '0' && ch <= '9') {
''', '''    *literal_percent = false;
    *zero_modifier = false;
    if (ch == 'z') {
        *zero_modifier = true;
        index += 1U;
        if (index >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[index];
    }
    if (ch >= '0' && ch <= '9') {
''', 1)
# %% must initialize modifier as well.
text = text.replace('''        *literal_percent = true;
        *operand_index = 0U;
''', '''        *literal_percent = true;
        *zero_modifier = false;
        *operand_index = 0U;
''', 1)

# Validator call.
text = text.replace('''        size_t operand_index;
        bool literal_percent;
''', '''        size_t operand_index;
        bool literal_percent;
        bool zero_modifier;
''', 1)
text = text.replace('''        if (!resolve_template_reference(
                inline_asm, operand_count, &index, &operand_index, &literal_percent)) {
''', '''        if (!resolve_template_reference(inline_asm,
                                        operand_count,
                                        &index,
                                        &operand_index,
                                        &literal_percent,
                                        &zero_modifier)) {
''', 1)
text = text.replace('''        (void)operand_index;
        (void)literal_percent;
''', '''        (void)operand_index;
        (void)literal_percent;
        (void)zero_modifier;
''', 1)

# Emitter call: replace the second declaration occurrence and resolver use.
emit_decl = '''        size_t operand_index;
        bool literal_percent;

        if (inline_asm->template_text[index] != '%') {
'''
emit_decl_new = '''        size_t operand_index;
        bool literal_percent;
        bool zero_modifier;

        if (inline_asm->template_text[index] != '%') {
'''
if emit_decl in text:
    text = text.replace(emit_decl, emit_decl_new, 1)
elif emit_decl_new not in text:
    raise SystemExit("emit modifier declaration anchor not found")
emit_resolve = '''        if (!resolve_template_reference(
                inline_asm, operand_count, &index, &operand_index, &literal_percent)) {
            return false;
        }
'''
emit_resolve_new = '''        if (!resolve_template_reference(inline_asm,
                                        operand_count,
                                        &index,
                                        &operand_index,
                                        &literal_percent,
                                        &zero_modifier)) {
            return false;
        }
'''
if emit_resolve in text:
    text = text.replace(emit_resolve, emit_resolve_new, 1)
elif emit_resolve_new not in text:
    raise SystemExit("emit resolver call anchor not found")

old_emit_choice = '''            register_name = operand_registers[operand_index];
            if (constraint_is_immediate(operand)) {
                if (!emit_immediate_operand(file,
                                            program,
                                            operand,
                                            inline_asm_id,
                                            operand_index,
                                            inline_asm->is_goto)) {
                    return false;
                }
            } else if (register_name == NULL) {
'''
new_emit_choice = '''            register_name = operand_registers[operand_index];
            if (zero_modifier && operand_uses_immediate(program, operand) &&
                operand_constant_zero(program, operand)) {
                if (fputs("zero", file) == EOF) {
                    return false;
                }
            } else if (operand_uses_immediate(program, operand)) {
                if (!emit_immediate_operand(file,
                                            program,
                                            operand,
                                            inline_asm_id,
                                            operand_index,
                                            inline_asm->is_goto)) {
                    return false;
                }
            } else if (register_name == NULL) {
'''
if old_emit_choice in text:
    text = text.replace(old_emit_choice, new_emit_choice, 1)
elif new_emit_choice not in text:
    raise SystemExit("operand emit choice anchor not found")

# rJ zero is a numeric immediate if it reaches ordinary immediate emission.
old_emit_immediate_symbol = '''    if (constraint_is(operand, "i") &&
        (symbol_name = symbolic_immediate_name(program, operand->expression)) != NULL) {
        return fputs(symbol_name, file) != EOF;
    }
'''
new_emit_immediate_symbol = '''    if (constraint_is(operand, "rJ") && operand_constant_zero(program, operand)) {
        return fputc('0', file) != EOF;
    }
    if (constraint_is(operand, "i") &&
        (symbol_name = symbolic_immediate_name(program, operand->expression)) != NULL) {
        return fputs(symbol_name, file) != EOF;
    }
'''
if old_emit_immediate_symbol in text:
    text = text.replace(old_emit_immediate_symbol, new_emit_immediate_symbol, 1)
elif new_emit_immediate_symbol not in text:
    raise SystemExit("rJ immediate emit anchor not found")

# Don't evaluate/stage rJ zero at runtime.
text = text.replace('''        if (constraint_is_immediate(operand)) {
            continue;
        }
        if (!minic_riscv64_emit_expression(file, program, function, operand->expression) ||
''', '''        if (operand_uses_immediate(program, operand)) {
            continue;
        }
        if (!minic_riscv64_emit_expression(file, program, function, operand->expression) ||
''', 1)
text = text.replace('''        if (constraint_is_immediate(&inline_asm->inputs[index])) {
            continue;
        }
''', '''        if (operand_uses_immediate(program, &inline_asm->inputs[index])) {
            continue;
        }
''', 1)

path.write_text(text)
