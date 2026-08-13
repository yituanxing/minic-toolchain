#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/cast_normalization.c")
text = path.read_text()

old = '''    size_t expression_index;
    size_t statement_index;
    size_t inline_asm_index;
    bool success;
'''
new = '''    size_t expression_index;
    size_t statement_index;
    size_t inline_asm_index;
    size_t cleanup_context_index;
    bool success;
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("cleanup remap local anchor not found")

old = '''    for (inline_asm_index = 0U; success && inline_asm_index < program->inline_asm_count;
         ++inline_asm_index) {
        const MinicInlineAsm *inline_asm;
        size_t operand_index;

        inline_asm = &program->inline_asms[inline_asm_index];
        for (operand_index = 0U; success && operand_index < inline_asm->output_count;
             ++operand_index) {
            MinicExpressionId old_id;

            old_id = inline_asm->outputs[operand_index].expression;
            success = old_id < old_expression_count && mapping != NULL &&
                      mapping[old_id] != MINIC_EXPRESSION_INVALID;
        }
        for (operand_index = 0U; success && operand_index < inline_asm->input_count;
             ++operand_index) {
            MinicExpressionId old_id;

            old_id = inline_asm->inputs[operand_index].expression;
            success = old_id < old_expression_count && mapping != NULL &&
                      mapping[old_id] != MINIC_EXPRESSION_INVALID;
        }
    }

    if (success) {
'''
new = '''    for (inline_asm_index = 0U; success && inline_asm_index < program->inline_asm_count;
         ++inline_asm_index) {
        const MinicInlineAsm *inline_asm;
        size_t operand_index;

        inline_asm = &program->inline_asms[inline_asm_index];
        for (operand_index = 0U; success && operand_index < inline_asm->output_count;
             ++operand_index) {
            MinicExpressionId old_id;

            old_id = inline_asm->outputs[operand_index].expression;
            success = old_id < old_expression_count && mapping != NULL &&
                      mapping[old_id] != MINIC_EXPRESSION_INVALID;
        }
        for (operand_index = 0U; success && operand_index < inline_asm->input_count;
             ++operand_index) {
            MinicExpressionId old_id;

            old_id = inline_asm->inputs[operand_index].expression;
            success = old_id < old_expression_count && mapping != NULL &&
                      mapping[old_id] != MINIC_EXPRESSION_INVALID;
        }
    }
    for (cleanup_context_index = 0U;
         success && cleanup_context_index < program->cleanup_context_count;
         ++cleanup_context_index) {
        MinicExpressionId old_id;

        old_id = program->cleanup_contexts[cleanup_context_index].cleanup_expression;
        success = old_id < old_expression_count && mapping != NULL &&
                  mapping[old_id] != MINIC_EXPRESSION_INVALID;
    }

    if (success) {
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("cleanup remap validation anchor not found")

old = '''        for (inline_asm_index = 0U; inline_asm_index < program->inline_asm_count;
             ++inline_asm_index) {
            MinicInlineAsm *inline_asm;
            size_t operand_index;

            inline_asm = &program->inline_asms[inline_asm_index];
            for (operand_index = 0U; operand_index < inline_asm->output_count; ++operand_index) {
                inline_asm->outputs[operand_index].expression =
                    mapping[inline_asm->outputs[operand_index].expression];
            }
            for (operand_index = 0U; operand_index < inline_asm->input_count; ++operand_index) {
                inline_asm->inputs[operand_index].expression =
                    mapping[inline_asm->inputs[operand_index].expression];
            }
        }
        free(program->expressions);
'''
new = '''        for (inline_asm_index = 0U; inline_asm_index < program->inline_asm_count;
             ++inline_asm_index) {
            MinicInlineAsm *inline_asm;
            size_t operand_index;

            inline_asm = &program->inline_asms[inline_asm_index];
            for (operand_index = 0U; operand_index < inline_asm->output_count; ++operand_index) {
                inline_asm->outputs[operand_index].expression =
                    mapping[inline_asm->outputs[operand_index].expression];
            }
            for (operand_index = 0U; operand_index < inline_asm->input_count; ++operand_index) {
                inline_asm->inputs[operand_index].expression =
                    mapping[inline_asm->inputs[operand_index].expression];
            }
        }
        for (cleanup_context_index = 0U;
             cleanup_context_index < program->cleanup_context_count;
             ++cleanup_context_index) {
            program->cleanup_contexts[cleanup_context_index].cleanup_expression =
                mapping[program->cleanup_contexts[cleanup_context_index].cleanup_expression];
        }
        free(program->expressions);
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("cleanup remap commit anchor not found")

path.write_text(text)
