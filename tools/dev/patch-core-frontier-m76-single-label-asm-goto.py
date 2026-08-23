#!/usr/bin/env python3
"""Add the first target-neutral Core seam for one-label GNU asm goto."""

from pathlib import Path

LOWER = Path("src/core/core_lower.c")
IR_H = Path("src/core/core_ir.h")
CODEGEN = Path("src/target/riscv64/core_codegen.c")
MARKER = "M76_SINGLE_LABEL_ASM_GOTO"


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M76 {name} anchor count={count}")
    return text.replace(old, new, 1)


def patch_ir_h() -> None:
    text = IR_H.read_text()
    if MARKER in text:
        return
    old = '''typedef struct MinicCoreInlineAsm {
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool has_memory_clobber;
} MinicCoreInlineAsm;
'''
    new = '''typedef struct MinicCoreInlineAsm {
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool has_memory_clobber;
    /* M76_SINGLE_LABEL_ASM_GOTO: preserve the control-flow target in Core
       instead of hiding it inside target assembly text. The first supported
       seam is one label plus one deferred immediate input. */
    bool is_goto;
    size_t source_inline_asm_id;
    MinicCoreBlockId goto_target;
} MinicCoreInlineAsm;
'''
    IR_H.write_text(replace_once(text, old, new, "ir-inline-asm"))


def patch_lower() -> None:
    text = LOWER.read_text()
    if MARKER in text:
        return

    helper_anchor = '''static MinicCoreLowerStatus lower_opaque_inline_asm(MinicCoreLowerContext *context,
                                                    const MinicStatement *statement) {
'''
    helper = r'''/* M76_SINGLE_LABEL_ASM_GOTO: admit the common GNU asm-goto seam without
   teaching Core any Linux/static-key meaning. Keep the initial contract narrow:
   one label, one read-only "i" operand whose value requires the existing
   deferred-immediate mechanism, no outputs/clobbers, and only %0/%l[label]/%%
   template references. */
static bool core_inline_asm_single_label_goto_supported(
    const MinicCoreLowerContext *context, const MinicInlineAsm *source) {
    const MinicExpression *input_expression;
    const MinicInlineAsmLabel *label;
    const MinicStatement *target_statement;
    char immediate_text[MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
    const char *resolved_text;
    size_t resolved_length;
    size_t cursor;
    bool saw_input;
    bool saw_label;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        source == NULL || !source->is_goto || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 0U ||
        source->input_count != 1U || source->inputs == NULL || source->label_count != 1U ||
        source->labels == NULL || source->register_clobber_count != 0U ||
        source->clobber_count != 0U || source->has_memory_clobber) {
        return false;
    }
    if (source->inputs[0].access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
        !core_inline_asm_constraint_is(&source->inputs[0], "i")) {
        return false;
    }
    input_expression =
        minic_c0_program_expression(context->body->program, source->inputs[0].expression);
    if (input_expression == NULL ||
        (!minic_type_is_integer(input_expression->type) &&
         !minic_type_is_pointer(input_expression->type))) {
        return false;
    }
    /* Resolved immediates already have the M61 path. M76 is deliberately the
       deferred-immediate asm-goto seam exposed by always-inline helpers. */
    if (core_inline_asm_immediate_text(context,
                                      &source->inputs[0],
                                      immediate_text,
                                      sizeof(immediate_text),
                                      &resolved_text,
                                      &resolved_length)) {
        return false;
    }
    label = &source->labels[0];
    if (label->name == NULL || label->name_length == 0U ||
        label->target_statement == MINIC_STATEMENT_INVALID) {
        return false;
    }
    target_statement =
        minic_c0_program_statement(context->body->program, label->target_statement);
    if (target_statement == NULL || target_statement->kind != MINIC_STATEMENT_LABEL) {
        return false;
    }

    cursor = 0U;
    saw_input = false;
    saw_label = false;
    while (cursor < source->template_length) {
        if (source->template_text[cursor] != '%') {
            cursor += 1U;
            continue;
        }
        if (cursor + 1U >= source->template_length) {
            return false;
        }
        if (source->template_text[cursor + 1U] == '%') {
            cursor += 2U;
            continue;
        }
        if (source->template_text[cursor + 1U] == '0') {
            saw_input = true;
            cursor += 2U;
            continue;
        }
        if (cursor + 3U < source->template_length &&
            source->template_text[cursor + 1U] == 'l' &&
            source->template_text[cursor + 2U] == '[') {
            size_t name_begin = cursor + 3U;
            size_t name_end = name_begin;
            while (name_end < source->template_length &&
                   source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (name_end >= source->template_length || name_end == name_begin ||
                name_end - name_begin != label->name_length ||
                memcmp(source->template_text + name_begin, label->name, label->name_length) != 0) {
                return false;
            }
            saw_label = true;
            cursor = name_end + 1U;
            continue;
        }
        return false;
    }
    return saw_input && saw_label;
}

static MinicCoreLowerStatus lower_opaque_inline_asm(MinicCoreLowerContext *context,
                                                    const MinicStatement *statement) {
'''
    text = replace_once(text, helper_anchor, helper, "lower-helper")

    source_anchor = '''    source = minic_c0_program_inline_asm(context->body->program, statement->inline_asm_id);
    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    /* M68_STRUCTURED_INLINE_ASM_OPTIONAL_INPUTS: M67's structured
'''
    source_replacement = '''    source = minic_c0_program_inline_asm(context->body->program, statement->inline_asm_id);
    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    if (core_inline_asm_single_label_goto_supported(context, source)) {
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

    /* M68_STRUCTURED_INLINE_ASM_OPTIONAL_INPUTS: M67's structured
'''
    text = replace_once(text, source_anchor, source_replacement, "lower-goto-dispatch")

    terminated_anchor = '''        if (block_terminated) {
            if (statement->kind != MINIC_STATEMENT_RETURN) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            continue;
        }
'''
    terminated_replacement = '''        if (block_terminated) {
            if (statement->kind == MINIC_STATEMENT_RETURN) {
                continue;
            }
            if (statement->kind != MINIC_STATEMENT_LABEL) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        }
'''
    text = replace_once(text, terminated_anchor, terminated_replacement, "label-after-terminator")

    label_branch_anchor = '''                if (context->block_id != label_block) {
                    status = set_branch(context, context->block_id, statement->span, label_block);
                    if (status != MINIC_CORE_LOWER_OK) return status;
                }
                context->block_id = label_block;
'''
    label_branch_replacement = '''                if (!block_terminated && context->block_id != label_block) {
                    status = set_branch(context, context->block_id, statement->span, label_block);
                    if (status != MINIC_CORE_LOWER_OK) return status;
                }
                context->block_id = label_block;
'''
    text = replace_once(text, label_branch_anchor, label_branch_replacement, "terminated-label-edge")
    LOWER.write_text(text)


def patch_codegen() -> None:
    text = CODEGEN.read_text()
    if MARKER in text:
        return

    supported_anchor = '''    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
           inline_asm->is_volatile;
}
'''
    supported_replacement = '''    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile) {
        return false;
    }
    /* M76_SINGLE_LABEL_ASM_GOTO: the target is explicit Core metadata even
       though the target-specific template remains opaque. */
    return !inline_asm->is_goto || inline_asm->goto_target < function->block_count;
}
'''
    text = replace_once(text, supported_anchor, supported_replacement, "codegen-supported")

    signature_anchor = '''static bool emit_opaque_inline_asm(FILE *file,
                                   const MinicCoreFunction *function,
                                   const MinicCoreInstruction *instruction) {
'''
    signature_replacement = '''static bool emit_opaque_inline_asm(FILE *file,
                                   const MinicCoreFunction *function,
                                   const char *symbol_name,
                                   const MinicCoreInstruction *instruction) {
'''
    text = replace_once(text, signature_anchor, signature_replacement, "emit-signature")

    body_anchor = '''    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            inline_asm->template_text[index + 1U] != '%') {
            return false;
        }
        if (fputc('%', file) == EOF) {
            return false;
        }
        index += 1U;
    }
    return fputc('\\n', file) != EOF;
}
'''
    body_replacement = '''    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (inline_asm->is_goto) {
        if (symbol_name == NULL || symbol_name[0] == '\\0' ||
            inline_asm->goto_target >= function->block_count ||
            fprintf(file,
                    "  # MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\\n"
                    "  .extern __minic_deferred_asm_immediate_%zu_0\\n"
                    "  ",
                    inline_asm->source_inline_asm_id) < 0) {
            return false;
        }
        for (index = 0U; index < inline_asm->template_length; ++index) {
            if (inline_asm->template_text[index] != '%') {
                if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                    return false;
                }
                continue;
            }
            if (index + 1U >= inline_asm->template_length) {
                return false;
            }
            if (inline_asm->template_text[index + 1U] == '%') {
                if (fputc('%', file) == EOF) {
                    return false;
                }
                index += 1U;
                continue;
            }
            if (inline_asm->template_text[index + 1U] == '0') {
                if (fprintf(file,
                            "__minic_deferred_asm_immediate_%zu_0",
                            inline_asm->source_inline_asm_id) < 0) {
                    return false;
                }
                index += 1U;
                continue;
            }
            if (index + 2U < inline_asm->template_length &&
                inline_asm->template_text[index + 1U] == 'l' &&
                inline_asm->template_text[index + 2U] == '[') {
                size_t name_end = index + 3U;
                while (name_end < inline_asm->template_length &&
                       inline_asm->template_text[name_end] != ']') {
                    name_end += 1U;
                }
                if (name_end >= inline_asm->template_length || name_end == index + 3U ||
                    fprintf(file,
                            ".L%s_core_bb%" PRIu32,
                            symbol_name,
                            inline_asm->goto_target) < 0) {
                    return false;
                }
                index = name_end;
                continue;
            }
            return false;
        }
        return fputc('\\n', file) != EOF;
    }
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            inline_asm->template_text[index + 1U] != '%') {
            return false;
        }
        if (fputc('%', file) == EOF) {
            return false;
        }
        index += 1U;
    }
    return fputc('\\n', file) != EOF;
}
'''
    text = replace_once(text, body_anchor, body_replacement, "emit-body")

    call_anchor = '''    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        return emit_opaque_inline_asm(file, function, instruction);
'''
    call_replacement = '''    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        return emit_opaque_inline_asm(file, function, symbol_name, instruction);
'''
    text = replace_once(text, call_anchor, call_replacement, "emit-call")
    CODEGEN.write_text(text)


def main() -> int:
    patch_ir_h()
    patch_lower()
    patch_codegen()
    print("M76 single-label deferred-immediate asm goto applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
