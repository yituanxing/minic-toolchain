#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
source = path.read_text()

marker = "M125_UNCLAIMED_ASM_CENSUS"
if marker in source:
    raise SystemExit("M125 asm census trace already present")

anchor = '''    /* M87_IMMEDIATE_ASM_FRONTIER_TRACE: report details only after every
       supported inline-asm path above has declined the statement. This keeps
       frontier observability from becoming a false first-error locator. */
'''
if source.count(anchor) != 1:
    raise SystemExit(f"M125 anchor count={source.count(anchor)}")

trace = r'''    /* M125_UNCLAIMED_ASM_CENSUS: diagnostic-only census inserted by CI.
       Report every inline-asm statement that reaches the final unsupported
       frontier, including output-bearing forms that M87 intentionally omits. */
    {
        size_t trace_index;

        (void)fprintf(stderr,
                      "M125_ASM_CENSUS function=%s volatile=%d goto=%d outputs=%zu inputs=%zu "
                      "labels=%zu reg_clobbers=%zu clobbers=%zu memory=%d template_length=%zu\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      source->is_volatile ? 1 : 0,
                      source->is_goto ? 1 : 0,
                      source->output_count,
                      source->input_count,
                      source->label_count,
                      source->register_clobber_count,
                      source->clobber_count,
                      source->has_memory_clobber ? 1 : 0,
                      source->template_length);
        for (trace_index = 0U; trace_index < source->output_count; ++trace_index) {
            const MinicInlineAsmOperand *trace_operand = &source->outputs[trace_index];
            const MinicExpression *trace_expression = minic_c0_program_expression(
                context->body->program, trace_operand->expression);

            (void)fprintf(stderr,
                          "M125_ASM_OPERAND function=%s role=out index=%zu access=%d "
                          "constraint=%.*s expr_kind=%d value_category=%d\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          trace_index,
                          (int)trace_operand->access,
                          (int)trace_operand->constraint_length,
                          trace_operand->constraint_text != NULL ? trace_operand->constraint_text : "",
                          trace_expression != NULL ? (int)trace_expression->kind : -1,
                          trace_expression != NULL ? (int)trace_expression->value_category : -1);
        }
        for (trace_index = 0U; trace_index < source->input_count; ++trace_index) {
            const MinicInlineAsmOperand *trace_operand = &source->inputs[trace_index];
            const MinicExpression *trace_expression = minic_c0_program_expression(
                context->body->program, trace_operand->expression);

            (void)fprintf(stderr,
                          "M125_ASM_OPERAND function=%s role=in index=%zu access=%d "
                          "constraint=%.*s expr_kind=%d value_category=%d\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          trace_index,
                          (int)trace_operand->access,
                          (int)trace_operand->constraint_length,
                          trace_operand->constraint_text != NULL ? trace_operand->constraint_text : "",
                          trace_expression != NULL ? (int)trace_expression->kind : -1,
                          trace_expression != NULL ? (int)trace_expression->value_category : -1);
        }
        for (trace_index = 0U; trace_index < source->register_clobber_count; ++trace_index) {
            const MinicInlineAsmRegisterClobber *trace_clobber =
                &source->register_clobbers[trace_index];

            (void)fprintf(stderr,
                          "M125_ASM_CLOBBER function=%s index=%zu name=%.*s\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          trace_index,
                          (int)trace_clobber->name_length,
                          trace_clobber->name != NULL ? trace_clobber->name : "");
        }
    }

'''

path.write_text(source.replace(anchor, trace + anchor, 1))
