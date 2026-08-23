#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M87_IMMEDIATE_ASM_FRONTIER_TRACE"


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M87 immediate-asm frontier trace already applied")
        return 0

    anchor = '''    if (!source->is_volatile || source->is_goto || source->template_text == NULL ||\n        source->template_length == 0U || source->output_count != 0U || source->input_count != 0U ||\n        source->label_count != 0U || source->register_clobber_count != 0U) {\n'''
    if text.count(anchor) != 1:
        raise SystemExit(f"M87 inline-asm fallback anchor count={text.count(anchor)}")

    insertion = r'''    /* M87_IMMEDIATE_ASM_FRONTIER_TRACE: when an input-only volatile asm was not
       claimed by M61, expose the semantic shape and each input's expression kind.
       This is generic frontier observability, not a Linux/BUG special case. */
    if (source->is_volatile && !source->is_goto && source->output_count == 0U &&
        source->input_count != 0U && source->inputs != NULL && source->label_count == 0U) {
        size_t trace_input_index;

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
        for (trace_input_index = 0U; trace_input_index < source->input_count; ++trace_input_index) {
            const MinicInlineAsmOperand *trace_operand = &source->inputs[trace_input_index];
            const MinicExpression *trace_expression = minic_c0_program_expression(
                context->body->program, trace_operand->expression);
            (void)fprintf(stderr,
                          "CORE_FAST_TRACE stage=inline-asm-immediate-input function=%s "
                          "index=%zu constraint=%.*s access=%d expr_kind=%d expr_type_base=%d\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          trace_input_index,
                          (int)trace_operand->constraint_length,
                          trace_operand->constraint_text != NULL ? trace_operand->constraint_text : "",
                          (int)trace_operand->access,
                          trace_expression != NULL ? (int)trace_expression->kind : -1,
                          trace_expression != NULL ? (int)trace_expression->type.base : -1);
        }
    }

'''
    PATH.write_text(text.replace(anchor, insertion + anchor, 1))
    print("M87 immediate-asm frontier trace applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
