#!/usr/bin/env python3
from pathlib import Path

lower_path = Path("src/core/core_lower_asm.c")
backend_path = Path("src/target/riscv64/core_codegen.c")
lower = lower_path.read_text()
backend = backend_path.read_text()

old_decls = '''    char immediate_text[MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
    const char *resolved_text;
    size_t resolved_length;
    size_t cursor;
'''
new_decls = '''    size_t cursor;
'''
if lower.count(old_decls) != 1:
    raise SystemExit(f"expected one M76 immediate declaration block, found {lower.count(old_decls)}")
lower = lower.replace(old_decls, new_decls, 1)

old_reject = '''    /* Resolved immediates already have the M61 path. M76 is deliberately the
       deferred-immediate asm-goto seam exposed by always-inline helpers. */
    if (core_inline_asm_immediate_text(context,
                                      &source->inputs[0],
                                      immediate_text,
                                      sizeof(immediate_text),
                                      &resolved_text,
                                      &resolved_length)) {
        return false;
    }
'''
new_reject = '''    /* M76R_RESOLVED_IMMEDIATE_ASM_GOTO: the same single-label control-flow
       seam owns both deferred and already-resolved immediate operands. The
       lowering path below bakes a resolved operand into target text before Core
       hands the opaque template to the backend; unresolved operands retain the
       discovery-only deferred symbol contract. */
'''
if lower.count(old_reject) != 1:
    raise SystemExit(f"expected one M76 resolved-immediate rejection, found {lower.count(old_reject)}")
lower = lower.replace(old_reject, new_reject, 1)

anchor = '''MinicCoreLowerStatus minic_core_lower_inline_asm(MinicCoreLowerContext *context,
'''
helper = r'''static bool core_inline_asm_single_label_goto_bake_input(
    const char *template_text,
    size_t template_length,
    const char *replacement,
    size_t replacement_length,
    char **template_out,
    size_t *template_length_out) {
    char *baked;
    size_t cursor;
    size_t output_length;
    size_t output_cursor;
    size_t replacement_count;

    if (template_text == NULL || template_length == 0U || replacement == NULL ||
        replacement_length == 0U || template_out == NULL || template_length_out == NULL) {
        return false;
    }
    output_length = template_length;
    replacement_count = 0U;
    for (cursor = 0U; cursor + 1U < template_length; ++cursor) {
        if (template_text[cursor] == '%' && template_text[cursor + 1U] == '0') {
            if (replacement_length >= 2U) {
                size_t growth = replacement_length - 2U;
                if (output_length > SIZE_MAX - growth) {
                    return false;
                }
                output_length += growth;
            } else {
                output_length -= 2U - replacement_length;
            }
            replacement_count += 1U;
            cursor += 1U;
        }
    }
    if (replacement_count == 0U || output_length == SIZE_MAX) {
        return false;
    }
    baked = (char *)malloc(output_length + 1U);
    if (baked == NULL) {
        return false;
    }
    output_cursor = 0U;
    for (cursor = 0U; cursor < template_length; ++cursor) {
        if (template_text[cursor] == '%' && cursor + 1U < template_length &&
            template_text[cursor + 1U] == '0') {
            (void)memcpy(baked + output_cursor, replacement, replacement_length);
            output_cursor += replacement_length;
            cursor += 1U;
            continue;
        }
        baked[output_cursor++] = template_text[cursor];
    }
    baked[output_cursor] = '\0';
    if (output_cursor != output_length) {
        free(baked);
        return false;
    }
    *template_out = baked;
    *template_length_out = output_length;
    return true;
}

'''
if lower.count(anchor) != 1:
    raise SystemExit("expected one inline asm lower entry anchor")
lower = lower.replace(anchor, helper + anchor, 1)

old_branch = '''    if (core_inline_asm_single_label_goto_supported(context, source)) {
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
        stored->is_goto = true;
        stored->source_inline_asm_id = (size_t)statement->inline_asm_id;
        stored->goto_target = target_block;

        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
        instruction.span = statement->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.inline_asm_id = inline_asm_id;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
'''
new_branch = r'''    if (core_inline_asm_single_label_goto_supported(context, source)) {
        char immediate_text[MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
        const char *resolved_text;
        size_t resolved_length;
        char *numeric_template;
        size_t numeric_template_length;
        MinicCoreBlockId target_block;
        MinicCoreInlineAsm *stored;
        MinicCoreLowerStatus status;
        bool input_resolved;

        status = ensure_statement_block(context, source->labels[0].target_statement, &target_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        input_resolved = core_inline_asm_immediate_text(context,
                                                        &source->inputs[0],
                                                        immediate_text,
                                                        sizeof(immediate_text),
                                                        &resolved_text,
                                                        &resolved_length);
        numeric_template = NULL;
        numeric_template_length = 0U;
        if (!core_inline_asm_single_label_goto_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (input_resolved) {
            char *baked_template = NULL;
            size_t baked_length = 0U;

            if (!core_inline_asm_single_label_goto_bake_input(numeric_template,
                                                               numeric_template_length,
                                                               resolved_text,
                                                               resolved_length,
                                                               &baked_template,
                                                               &baked_length)) {
                free(numeric_template);
                return MINIC_CORE_LOWER_ERROR;
            }
            free(numeric_template);
            numeric_template = baked_template;
            numeric_template_length = baked_length;
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
        stored->is_goto = true;
        stored->source_inline_asm_id = input_resolved
                                           ? SIZE_MAX
                                           : (size_t)statement->inline_asm_id;
        stored->goto_target = target_block;

        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
        instruction.span = statement->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.inline_asm_id = inline_asm_id;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
'''
if lower.count(old_branch) != 1:
    raise SystemExit(f"expected one M76 lower branch, found {lower.count(old_branch)}")
lower = lower.replace(old_branch, new_branch, 1)

old_backend_prefix = r'''    if (inline_asm->is_goto) {
        if (symbol_name == NULL || symbol_name[0] == '\0' ||
            inline_asm->goto_target >= function->block_count ||
            fprintf(file,
                    "  # MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\n"
                    "  .extern __minic_deferred_asm_immediate_%zu_0\n"
                    "  ",
                    inline_asm->source_inline_asm_id) < 0) {
            return false;
        }
'''
new_backend_prefix = r'''    if (inline_asm->is_goto) {
        bool deferred_immediate = inline_asm->source_inline_asm_id != SIZE_MAX;

        if (symbol_name == NULL || symbol_name[0] == '\0' ||
            inline_asm->goto_target >= function->block_count) {
            return false;
        }
        if (deferred_immediate) {
            if (fprintf(file,
                        "  # MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\n"
                        "  .extern __minic_deferred_asm_immediate_%zu_0\n"
                        "  ",
                        inline_asm->source_inline_asm_id) < 0) {
                return false;
            }
        } else if (fprintf(file, "  ") < 0) {
            return false;
        }
'''
if backend.count(old_backend_prefix) != 1:
    raise SystemExit(f"expected one RV64 asm-goto prefix, found {backend.count(old_backend_prefix)}")
backend = backend.replace(old_backend_prefix, new_backend_prefix, 1)

old_backend_operand = r'''            if (inline_asm->template_text[index + 1U] == '0') {
                if (fprintf(file,
                            "__minic_deferred_asm_immediate_%zu_0",
                            inline_asm->source_inline_asm_id) < 0) {
                    return false;
                }
                index += 1U;
                continue;
            }
'''
new_backend_operand = r'''            if (inline_asm->template_text[index + 1U] == '0') {
                if (!deferred_immediate ||
                    fprintf(file,
                            "__minic_deferred_asm_immediate_%zu_0",
                            inline_asm->source_inline_asm_id) < 0) {
                    return false;
                }
                index += 1U;
                continue;
            }
'''
if backend.count(old_backend_operand) != 1:
    raise SystemExit(f"expected one RV64 deferred operand rewrite, found {backend.count(old_backend_operand)}")
backend = backend.replace(old_backend_operand, new_backend_operand, 1)

lower_path.write_text(lower)
backend_path.write_text(backend)
print("MINIC_INLINE_SPECIALIZATION_RESOLVED_ASM_GOTO_V0=APPLIED")
