#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"anchor not found in {path}")
    p.write_text(text.replace(old, new, 1))


path = "src/core/core_lower.c"

# Batch E: M76 already owns the one-label/one-deferred-immediate asm-goto
# semantic seam. GNU named references such as %[ext] are only syntax for the
# same sole input as %0. Accept that syntax at the frontend/Core boundary, then
# normalize it to %0 before storing the Core opaque-asm template so backends do
# not need frontend operand names.
old = '''        if (source->template_text[cursor + 1U] == '0') {
            saw_input = true;
            cursor += 2U;
            continue;
        }
        if (cursor + 3U < source->template_length &&
            source->template_text[cursor + 1U] == 'l' &&
            source->template_text[cursor + 2U] == '[') {
'''
new = '''        if (source->template_text[cursor + 1U] == '0') {
            saw_input = true;
            cursor += 2U;
            continue;
        }
        if (source->template_text[cursor + 1U] == '[') {
            const MinicInlineAsmOperand *input = &source->inputs[0];
            size_t name_begin = cursor + 2U;
            size_t name_end = name_begin;

            while (name_end < source->template_length &&
                   source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (input->name == NULL || input->name_length == 0U ||
                name_end >= source->template_length || name_end == name_begin ||
                name_end - name_begin != input->name_length ||
                memcmp(source->template_text + name_begin, input->name, input->name_length) != 0) {
                return false;
            }
            saw_input = true;
            cursor = name_end + 1U;
            continue;
        }
        if (cursor + 3U < source->template_length &&
            source->template_text[cursor + 1U] == 'l' &&
            source->template_text[cursor + 2U] == '[') {
'''
replace_once(path, old, new)

old = '''    return saw_input && saw_label;
}

static MinicCoreLowerStatus lower_opaque_inline_asm'''
new = '''    return saw_input && saw_label;
}

static bool core_inline_asm_single_label_goto_numeric_template(
    const MinicInlineAsm *source, char **template_out, size_t *template_length_out) {
    char *normalized;
    size_t cursor;
    size_t output_length;

    if (source == NULL || template_out == NULL || template_length_out == NULL ||
        source->template_text == NULL || source->inputs == NULL || source->input_count != 1U) {
        return false;
    }
    normalized = (char *)malloc(source->template_length + 1U);
    if (normalized == NULL) {
        return false;
    }
    cursor = 0U;
    output_length = 0U;
    while (cursor < source->template_length) {
        if (source->template_text[cursor] == '%' && cursor + 1U < source->template_length &&
            source->template_text[cursor + 1U] == '[') {
            const MinicInlineAsmOperand *input = &source->inputs[0];
            size_t name_begin = cursor + 2U;
            size_t name_end = name_begin;

            while (name_end < source->template_length &&
                   source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (input->name == NULL || input->name_length == 0U ||
                name_end >= source->template_length || name_end == name_begin ||
                name_end - name_begin != input->name_length ||
                memcmp(source->template_text + name_begin, input->name, input->name_length) != 0) {
                free(normalized);
                return false;
            }
            normalized[output_length++] = '%';
            normalized[output_length++] = '0';
            cursor = name_end + 1U;
            continue;
        }
        normalized[output_length++] = source->template_text[cursor++];
    }
    normalized[output_length] = '\\0';
    *template_out = normalized;
    *template_length_out = output_length;
    return true;
}

static MinicCoreLowerStatus lower_opaque_inline_asm'''
replace_once(path, old, new)

old = '''    if (core_inline_asm_single_label_goto_supported(context, source)) {
        MinicCoreBlockId target_block;
        MinicCoreInlineAsm *stored;
        MinicCoreLowerStatus status;

        status = ensure_statement_block(context, source->labels[0].target_statement, &target_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                       source->template_text,
                                                       source->template_length,
                                                       true,
                                                       false,
                                                       &inline_asm_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        stored = &context->function->inline_asms[inline_asm_id];
'''
new = '''    if (core_inline_asm_single_label_goto_supported(context, source)) {
        char *numeric_template;
        size_t numeric_template_length;
        MinicCoreBlockId target_block;
        MinicCoreInlineAsm *stored;
        MinicCoreLowerStatus status;

        status = ensure_statement_block(context, source->labels[0].target_statement, &target_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        numeric_template = NULL;
        numeric_template_length = 0U;
        if (!core_inline_asm_single_label_goto_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                       numeric_template,
                                                       numeric_template_length,
                                                       true,
                                                       false,
                                                       &inline_asm_id)) {
            free(numeric_template);
            return MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
        stored = &context->function->inline_asms[inline_asm_id];
'''
replace_once(path, old, new)

print("CORE_BATCH_E_PATCHED named-sole-input asm-goto normalization")
