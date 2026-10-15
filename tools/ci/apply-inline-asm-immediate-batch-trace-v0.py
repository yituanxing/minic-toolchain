#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower_asm.c")
text = p.read_text()

anchor = r'''    /* Linux RISC-V CSR helpers use one write-only register output plus an rK
       input and a memory clobber.  M126A below intentionally accepts rK as a
       scalar-register alternative, but doing so first materializes a constant
       input (and the output address) in the frame.  Prefer the immediate
       alternative before generic structured lowering whenever every input can
       be baked into the template.  If specialization fails, M126A retains the
       original register fallback semantics. */
'''

diag = r'''    if (getenv("MINIC_ASM_BATCH_TRACE") != NULL &&
        source->output_count == 1U && source->input_count != 0U &&
        source->outputs != NULL && source->inputs != NULL) {
        const MinicInlineAsmOperand *trace_output = &source->outputs[0];
        const MinicExpression *trace_output_expression =
            minic_c0_program_expression(context->body->program, trace_output->expression);
        char *trace_template = NULL;
        size_t trace_template_length = 0U;
        bool trace_batch =
            core_inline_asm_specialize_register_output_immediates(
                context, source, &trace_template, &trace_template_length);
        size_t trace_input_index;

        (void)fprintf(stderr,
                      "ASM_BATCH_PROBE function=%s volatile=%d goto=%d outputs=%zu inputs=%zu "
                      "labels=%zu reg_clobbers=%zu clobbers=%zu memory=%d "
                      "out_access=%d out_constraint=%.*s out_expr_kind=%d out_value_category=%d "
                      "batch=%d batch_len=%zu\n",
                      context->source_function != NULL &&
                              context->source_function->name != NULL
                          ? context->source_function->name
                          : "?",
                      source->is_volatile ? 1 : 0,
                      source->is_goto ? 1 : 0,
                      source->output_count,
                      source->input_count,
                      source->label_count,
                      source->register_clobber_count,
                      source->clobber_count,
                      source->has_memory_clobber ? 1 : 0,
                      (int)trace_output->access,
                      (int)trace_output->constraint_length,
                      trace_output->constraint_text != NULL
                          ? trace_output->constraint_text
                          : "",
                      trace_output_expression != NULL
                          ? (int)trace_output_expression->kind
                          : -1,
                      trace_output_expression != NULL
                          ? (int)trace_output_expression->value_category
                          : -1,
                      trace_batch ? 1 : 0,
                      trace_template_length);
        free(trace_template);

        for (trace_input_index = 0U;
             trace_input_index < source->input_count;
             ++trace_input_index) {
            const MinicInlineAsmOperand *trace_input =
                &source->inputs[trace_input_index];
            const MinicExpression *trace_input_expression =
                minic_c0_program_expression(
                    context->body->program, trace_input->expression);
            char trace_integer[MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
            const char *trace_text = NULL;
            size_t trace_length = 0U;
            bool trace_resolved =
                core_inline_asm_immediate_text(
                    context,
                    trace_input,
                    trace_integer,
                    sizeof(trace_integer),
                    &trace_text,
                    &trace_length);

            (void)fprintf(stderr,
                          "ASM_BATCH_INPUT function=%s index=%zu access=%d constraint=%.*s "
                          "expr_kind=%d value_category=%d resolved=%d value=%.*s\n",
                          context->source_function != NULL &&
                                  context->source_function->name != NULL
                              ? context->source_function->name
                              : "?",
                          trace_input_index,
                          (int)trace_input->access,
                          (int)trace_input->constraint_length,
                          trace_input->constraint_text != NULL
                              ? trace_input->constraint_text
                              : "",
                          trace_input_expression != NULL
                              ? (int)trace_input_expression->kind
                              : -1,
                          trace_input_expression != NULL
                              ? (int)trace_input_expression->value_category
                              : -1,
                          trace_resolved ? 1 : 0,
                          trace_resolved ? (int)trace_length : 0,
                          trace_resolved && trace_text != NULL ? trace_text : "");
        }
    }

''';

if text.count(anchor) != 1:
    raise SystemExit(f"expected one RK early anchor, found {text.count(anchor)}")
text = text.replace(anchor, diag + anchor, 1)
p.write_text(text)
print("MINIC_INLINE_ASM_IMMEDIATE_BATCH_TRACE_V0=APPLIED")
