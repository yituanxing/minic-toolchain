#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M87_IMMEDIATE_ASM_FRONTIER_TRACE"


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M87 immediate-asm frontier trace already applied")
        return 0

    source_anchor = '''    source = minic_c0_program_inline_asm(context->body->program, statement->inline_asm_id);
    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
'''
    if text.count(source_anchor) != 1:
        raise SystemExit(f"M87 source anchor count={text.count(source_anchor)}")
    source_trace = source_anchor + r'''
    /* M87_IMMEDIATE_ASM_FRONTIER_TRACE: multi-input asm is a high-value
       frontier family. Report its semantic shape and whether each input can be
       specialized by the existing M61 immediate resolver. */
    if (source->input_count >= 3U && source->inputs != NULL) {
        size_t trace_input_index;

        (void)fprintf(stderr,
                      "CORE_FAST_TRACE stage=inline-asm-shape function=%s volatile=%d goto=%d "
                      "outputs=%zu inputs=%zu labels=%zu reg_clobbers=%zu clobbers=%zu memory=%d "
                      "template_length=%zu span=%zu:%zu\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      source->is_volatile ? 1 : 0,
                      source->is_goto ? 1 : 0,
                      source->output_count,
                      source->input_count,
                      source->label_count,
                      source->register_clobber_count,
                      source->clobber_count,
                      source->has_memory_clobber ? 1 : 0,
                      source->template_length,
                      statement->span.begin.line,
                      statement->span.begin.column);
        for (trace_input_index = 0U; trace_input_index < source->input_count; ++trace_input_index) {
            const MinicInlineAsmOperand *trace_operand = &source->inputs[trace_input_index];
            const MinicExpression *trace_expression = minic_c0_program_expression(
                context->body->program, trace_operand->expression);
            char trace_integer_text[MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
            const char *trace_resolved_text = NULL;
            size_t trace_resolved_length = 0U;
            bool trace_resolved = core_inline_asm_immediate_text(
                context,
                trace_operand,
                trace_integer_text,
                sizeof(trace_integer_text),
                &trace_resolved_text,
                &trace_resolved_length);
            (void)trace_resolved_text;
            (void)fprintf(stderr,
                          "CORE_FAST_TRACE stage=inline-asm-input function=%s index=%zu "
                          "constraint=%.*s access=%d expr_kind=%d immediate_resolved=%d "
                          "resolved_length=%zu\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          trace_input_index,
                          (int)trace_operand->constraint_length,
                          trace_operand->constraint_text != NULL ? trace_operand->constraint_text : "",
                          (int)trace_operand->access,
                          trace_expression != NULL ? (int)trace_expression->kind : -1,
                          trace_resolved ? 1 : 0,
                          trace_resolved_length);
        }
    }
'''
    text = text.replace(source_anchor, source_trace, 1)

    fallback_anchor = '''    if (!source->is_volatile || source->is_goto || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 0U || source->input_count != 0U ||
        source->label_count != 0U || source->register_clobber_count != 0U) {
'''
    if text.count(fallback_anchor) != 1:
        raise SystemExit(f"M87 inline-asm fallback anchor count={text.count(fallback_anchor)}")

    fallback_trace = r'''    if (source->is_volatile && !source->is_goto && source->output_count == 0U &&
        source->input_count != 0U && source->inputs != NULL && source->label_count == 0U) {
        (void)fprintf(stderr,
                      "CORE_FAST_TRACE stage=inline-asm-immediate reason=unclaimed "
                      "function=%s inputs=%zu reg_clobbers=%zu clobbers=%zu memory=%d span=%zu:%zu\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      source->input_count,
                      source->register_clobber_count,
                      source->clobber_count,
                      source->has_memory_clobber ? 1 : 0,
                      statement->span.begin.line,
                      statement->span.begin.column);
    }

'''
    text = text.replace(fallback_anchor, fallback_trace + fallback_anchor, 1)
    PATH.write_text(text)
    print("M87 immediate-asm frontier trace applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
