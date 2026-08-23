from pathlib import Path

MARKER = 'M69_STRUCTURED_ASM_REGISTER_OR_ZERO'


def replace_once(text: str, anchor: str, replacement: str, label: str) -> str:
    count = text.count(anchor)
    if count != 1:
        raise SystemExit(f'M69 {label} anchor count={count}')
    return text.replace(anchor, replacement, 1)


# Core lowering: preserve one-character GNU operand modifiers while resolving
# symbolic operands to Core's numeric binding indices. This is target-neutral:
# the modifier remains opaque template text for the backend to interpret.
path = Path('src/core/core_lower.c')
text = path.read_text()
if MARKER not in text:
    start = text.find('static bool core_inline_asm_numeric_template(')
    end = text.find('\nstatic MinicCoreLowerStatus lower_opaque_inline_asm(', start)
    if start < 0 or end < 0:
        raise SystemExit('M69 numeric-template function not found')
    replacement = r'''/* M69_STRUCTURED_ASM_REGISTER_OR_ZERO: normalize named operands while
   preserving a single GNU operand print modifier (for example RISC-V %z).
   Core does not interpret the modifier; the target backend owns that dialect. */
static bool core_inline_asm_numeric_template(const MinicInlineAsm *source,
                                             char **template_out,
                                             size_t *template_length_out) {
    size_t cursor;
    size_t output_length;
    char *normalized;

    if (source == NULL || template_out == NULL || template_length_out == NULL ||
        source->template_text == NULL || source->template_length == 0U ||
        source->output_count + source->input_count > 10U) {
        return false;
    }
    normalized = (char *)malloc(source->template_length + 1U);
    if (normalized == NULL) {
        return false;
    }
    cursor = 0U;
    output_length = 0U;
    while (cursor < source->template_length) {
        size_t operand_index;
        char modifier;

        if (source->template_text[cursor] != '%') {
            normalized[output_length++] = source->template_text[cursor++];
            continue;
        }
        if (cursor + 1U >= source->template_length) {
            free(normalized);
            return false;
        }
        normalized[output_length++] = '%';
        cursor += 1U;
        if (source->template_text[cursor] == '%') {
            normalized[output_length++] = '%';
            cursor += 1U;
            continue;
        }
        modifier = '\0';
        if ((source->template_text[cursor] >= 'A' && source->template_text[cursor] <= 'Z') ||
            (source->template_text[cursor] >= 'a' && source->template_text[cursor] <= 'z')) {
            modifier = source->template_text[cursor++];
            if (cursor >= source->template_length) {
                free(normalized);
                return false;
            }
        }
        if (source->template_text[cursor] >= '0' && source->template_text[cursor] <= '9') {
            operand_index = (size_t)(source->template_text[cursor] - '0');
            if (operand_index >= source->output_count + source->input_count) {
                free(normalized);
                return false;
            }
            if (modifier != '\0') {
                normalized[output_length++] = modifier;
            }
            normalized[output_length++] = source->template_text[cursor++];
            continue;
        }
        if (source->template_text[cursor] == '[') {
            size_t name_begin = cursor + 1U;
            size_t name_end = name_begin;
            while (name_end < source->template_length && source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (name_end >= source->template_length || name_end == name_begin ||
                !core_inline_asm_named_operand_index(source,
                                                     source->template_text + name_begin,
                                                     name_end - name_begin,
                                                     &operand_index) ||
                operand_index > 9U) {
                free(normalized);
                return false;
            }
            if (modifier != '\0') {
                normalized[output_length++] = modifier;
            }
            normalized[output_length++] = (char)('0' + operand_index);
            cursor = name_end + 1U;
            continue;
        }
        free(normalized);
        return false;
    }
    normalized[output_length] = '\0';
    *template_out = normalized;
    *template_length_out = output_length;
    return true;
}
'''
    text = text[:start] + replacement + text[end:]
    text = replace_once(
        text,
        '''            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                !core_inline_asm_constraint_is(operand, "r") || expression == NULL ||
''',
        '''            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                (!core_inline_asm_constraint_is(operand, "r") &&
                 !core_inline_asm_constraint_is(operand, "rJ")) || expression == NULL ||
''',
        'rJ-admission')
    path.write_text(text)
else:
    print('M69 core_lower.c already applied')


# RV64 owns the meaning of the RISC-V %z print modifier. Structured scalar
# inputs are deliberately materialized in registers, so %zN and %N emit the
# same selected register; the immediate-zero optimization is optional.
path = Path('src/target/riscv64/core_codegen.c')
text = path.read_text()
if MARKER not in text:
    anchor = '''        ch = (unsigned char)inline_asm->template_text[++template_index];
        if (ch == '%') {
            continue;
        }
        if (ch < '0' || ch > '9' || !bound[(size_t)(ch - '0')]) {
            return false;
        }
'''
    repl = '''        ch = (unsigned char)inline_asm->template_text[++template_index];
        if (ch == '%') {
            continue;
        }
        /* M69_STRUCTURED_ASM_REGISTER_OR_ZERO: %z is a RISC-V operand
           printer modifier. Inputs in this Core tier are always materialized
           registers, so accepting it does not change the selected value. */
        if (ch == 'z') {
            if (template_index + 1U >= inline_asm->template_length) {
                return false;
            }
            ch = (unsigned char)inline_asm->template_text[++template_index];
        }
        if (ch < '0' || ch > '9' || !bound[(size_t)(ch - '0')]) {
            return false;
        }
'''
    text = replace_once(text, anchor, repl, 'backend-template-validation')

    anchor = '''        ch = (unsigned char)inline_asm->template_text[index];
        if (ch == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
            continue;
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
'''
    repl = '''        ch = (unsigned char)inline_asm->template_text[index];
        if (ch == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
            continue;
        }
        if (ch == 'z') {
            if (++index >= inline_asm->template_length) {
                return false;
            }
            ch = (unsigned char)inline_asm->template_text[index];
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
'''
    text = replace_once(text, anchor, repl, 'backend-template-emission')
    path.write_text(text)
else:
    print('M69 core_codegen.c already applied')

print('M69 structured asm register-or-zero operands applied')
