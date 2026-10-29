#include "core/core_lower_asm_internal.h"

#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include "target/data_layout.h"
#include "target/backend.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Core inline-asm driver: preserve diagnostics and first-claim-wins
 * admission order; no operand or target machine instruction semantics here. */

/* Diagnostics are outside the emission dispatcher. Their original
 * relative placement (before first candidate / after every candidate)
 * is intentional and must not change. */
static void core_asm_trace_input_batch(
    const MinicCoreLowerContext *context, const MinicInlineAsm *source) {
    if (getenv("MINIC_ASM_BATCH_TRACE") != NULL &&
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
}

static void core_asm_trace_unclaimed(
    const MinicCoreLowerContext *context, const MinicInlineAsm *source) {
    /* M87_IMMEDIATE_ASM_FRONTIER_TRACE: report details only after every
       supported inline-asm path above has declined the statement. This keeps
       frontier observability from becoming a false first-error locator. */
    if (source->is_volatile && !source->is_goto && source->output_count == 0U &&
        source->input_count != 0U && source->inputs != NULL && source->label_count == 0U) {
        size_t trace_input_index;

        (void)fprintf(stderr,
                      "CORE_ASM_DETAIL reason=unclaimed function=%s inputs=%zu "
                      "reg_clobbers=%zu clobbers=%zu memory=%d template_length=%zu\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      source->input_count,
                      source->register_clobber_count,
                      source->clobber_count,
                      source->has_memory_clobber ? 1 : 0,
                      source->template_length);
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
                          "CORE_ASM_DETAIL input function=%s index=%zu constraint=%.*s "
                          "access=%d expr_kind=%d immediate_resolved=%d resolved_length=%zu\n",
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

}

/* Diagnostic-only census for original GNU Linux.  A rejected source ASM
 * must not become an opaque machine instruction just to improve C-owner
 * totals.  Include both unclaimed forms and a claimed contract whose Core
 * operand lowering failed. Fingerprint template and constraints so that
 * one target machine-contract family can be fixed at a time.  The same
 * algorithm is used for every ISA; no target register or opcode knowledge
 * belongs in this Core trace. */
static uint64_t core_asm_census_hash_bytes(uint64_t hash,
                                            const char *bytes, size_t length) {
    size_t i;
    if (bytes == NULL) return hash;
    for (i = 0U; i < length; ++i) {
        hash ^= (unsigned char)bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t core_asm_census_hash_number(uint64_t hash, size_t value) {
    size_t i;
    uint64_t bits = (uint64_t)value;
    for (i = 0U; i < 8U; ++i) {
        hash ^= (unsigned char)(bits & UINT64_C(255));
        hash *= UINT64_C(1099511628211);
        bits >>= 8U;
    }
    return hash;
}

static void core_asm_trace_frontier(
    const MinicCoreLowerContext *context, const MinicInlineAsm *source,
    const char *reason) {
    uint64_t template_hash = UINT64_C(14695981039346656037);
    uint64_t signature_hash;
    char preview[33];
    size_t i, n = 0U;

    if (getenv("MINIC_ASM_FRONTIER_TRACE") == NULL || context == NULL ||
        context->source_function == NULL || context->source_function->name == NULL ||
        source == NULL || reason == NULL) return;
    template_hash = core_asm_census_hash_bytes(
        template_hash, source->template_text, source->template_length);
    signature_hash = core_asm_census_hash_number(template_hash, source->output_count);
    signature_hash = core_asm_census_hash_number(signature_hash, source->input_count);
    signature_hash = core_asm_census_hash_number(signature_hash, source->label_count);
    signature_hash = core_asm_census_hash_number(signature_hash, source->register_clobber_count);
    signature_hash = core_asm_census_hash_number(signature_hash, source->clobber_count);
    signature_hash = core_asm_census_hash_number(signature_hash,
                                                 source->has_memory_clobber ? 1U : 0U);
    signature_hash = core_asm_census_hash_number(signature_hash, source->is_goto ? 1U : 0U);
    signature_hash = core_asm_census_hash_number(signature_hash, source->is_volatile ? 1U : 0U);
    for (i = 0U; i < source->output_count && source->outputs != NULL; ++i) {
        const MinicInlineAsmOperand *op = &source->outputs[i];
        signature_hash = core_asm_census_hash_bytes(
            signature_hash, op->constraint_text, op->constraint_length);
        signature_hash = core_asm_census_hash_number(signature_hash, (size_t)op->access);
    }
    for (i = 0U; i < source->input_count && source->inputs != NULL; ++i) {
        const MinicInlineAsmOperand *op = &source->inputs[i];
        signature_hash = core_asm_census_hash_bytes(
            signature_hash, op->constraint_text, op->constraint_length);
        signature_hash = core_asm_census_hash_number(signature_hash, (size_t)op->access);
    }
    for (i = 0U; i < source->register_clobber_count &&
                 source->register_clobbers != NULL; ++i) {
        const MinicInlineAsmRegisterClobber *clobber = &source->register_clobbers[i];
        signature_hash = core_asm_census_hash_bytes(
            signature_hash, clobber->name, clobber->name_length);
    }
    for (i = 0U; i < source->template_length && n < sizeof(preview) - 1U; ++i) {
        const unsigned char c = (unsigned char)source->template_text[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) preview[n++] = (char)c;
        else if (n != 0U && preview[n - 1U] != '_') preview[n++] = '_';
    }
    if (n == 0U) preview[n++] = '_';
    preview[n] = '\0';
    (void)fprintf(stderr,
                  "CORE_ASM_FRONTIER function=%s reason=%s template_length=%zu "
                  "template_hash=%016" PRIx64 " signature_hash=%016" PRIx64
                  " preview=%s outputs=%zu inputs=%zu labels=%zu "
                  "reg_clobbers=%zu clobbers=%zu memory=%d goto=%d volatile=%d\n",
                  context->source_function->name, reason, source->template_length,
                  template_hash, signature_hash, preview, source->output_count,
                  source->input_count, source->label_count,
                  source->register_clobber_count, source->clobber_count,
                  source->has_memory_clobber ? 1 : 0, source->is_goto ? 1 : 0,
                  source->is_volatile ? 1 : 0);
}

static MinicCoreLowerStatus core_asm_trace_status(
    const MinicCoreLowerContext *context, const MinicInlineAsm *source,
    MinicCoreLowerStatus status, const char *reason) {
    if (status != MINIC_CORE_LOWER_OK)
        core_asm_trace_frontier(context, source, reason);
    return status;
}

MinicCoreLowerStatus minic_core_lower_inline_asm(MinicCoreLowerContext *context,
                                                    const MinicStatement *statement) {
    const MinicInlineAsm *source;
    MinicCoreInlineAsmId inline_asm_id;
    MinicCoreInstruction instruction;
    MinicCoreLowerStatus status;
    bool handled;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL ||
        statement->inline_asm_id == MINIC_INLINE_ASM_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source = minic_c0_program_inline_asm(context->body->program, statement->inline_asm_id);
    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    /* GNU allows more operands than our fixed-size Core representation.
     * Reject the entire shape before any target or legacy lowering can claim
     * it, including the overflow-safe mixed output/input boundary. */
    if (source->output_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT ||
        source->input_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT -
                                  source->output_count) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }


    core_asm_trace_input_batch(context, source);

    /* Target-selected GNU register constraints still transport a canonical
     * structured Core effect. Preflight does not mutate on an unsupported
     * shape; other targets retain the original ordered lowering. */
    handled = false;
    status = core_asm_try_target_register_contract(context, statement, source, &handled);
    if (handled) return core_asm_trace_status(context, source, status, "target_lower");
    /* A target-qualified instruction with GNU input operands must never
     * fall back to Core's general scalar/opaque inline-asm machinery after
     * its target rejects the specific constraints or instruction suffix.
     * This is one shared ownership gate, with ISA mnemonics supplied solely
     * by each backend descriptor (not Frontend/Core arch conditionals). */
    if (minic_backend_requires_qualified_input_asm(
            minic_backend_for_target(context->target), source)) {
        core_asm_trace_frontier(context, source, "target_required");
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    handled = false;
    status = core_asm_try_immediate_output_csr(context, statement, source, &handled);
    if (handled) return core_asm_trace_status(context, source, status, "immediate_output");

    handled = false;
    status = core_asm_try_structured_roles_and_asm_goto(context, statement, source, &handled);
    if (handled) return core_asm_trace_status(context, source, status, "structured_roles");

    handled = false;
    status = core_asm_try_atomic_and_immediate_forms(context, statement, source, &handled);
    if (handled) return core_asm_trace_status(context, source, status, "atomic_immediate");

    handled = false;
    status = core_asm_try_mixed_memory_forms(context, statement, source, &handled);
    if (handled) return core_asm_trace_status(context, source, status, "mixed_memory");

    handled = false;
    status = core_asm_try_readwrite_and_output_specialization(context, statement, source, &handled);
    if (handled) return core_asm_trace_status(context, source, status, "readwrite_output");

    handled = false;
    status = core_asm_try_empty_barrier_scalar_forms(context, statement, source, &handled);
    if (handled) return core_asm_trace_status(context, source, status, "empty_barrier");

    core_asm_trace_unclaimed(context, source);

    if (!source->is_volatile || source->is_goto || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 0U || source->input_count != 0U ||
        source->label_count != 0U || source->register_clobber_count != 0U) {
        core_asm_trace_frontier(context, source, "no_handler");
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                   source->template_text,
                                                   source->template_length,
                                                   source->is_volatile,
                                                   source->has_memory_clobber,
                                                   &inline_asm_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }
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
