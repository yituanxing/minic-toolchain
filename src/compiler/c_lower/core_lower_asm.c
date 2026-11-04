#include "compiler/c_lower/core_lower_asm_internal.h"

#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include "frontend/ast_traversal.h"
#include "target/data_layout.h"
#include "target/backend_source.h"

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
    /* The same source-form fingerprint is attached to admitted structured
     * Core instructions. Failed forms use that identical identity here;
     * distinct diagnostic paths must not carry a second hash algorithm. */
    signature_hash = minic_source_backend_asm_source_form_id(source);
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

/* Keep source declaration knowledge at this one AST -> Core seam. The
 * reachable Core ASM effect owns typed linker-name references afterwards;
 * Module closure must never reopen C operand expressions. */
typedef struct CoreAsmSymbolCapture {
    MinicCoreLowerContext *lower;
    MinicCoreInlineAsmId asm_id;
    unsigned int depth;
    bool uninspectable;
    bool failed;
} CoreAsmSymbolCapture;

/* Symbolic GNU `i` facts are read at ASM admission time. Operand lowering
 * can write/escape an output local *after* its compile-time immediate has
 * already been baked into the template. A post-handler query of that local
 * would then incorrectly lose the input's original relocation target. */
typedef struct CoreAsmImmediateSnapshot {
    MinicCoreInlineAsmSymbolReference *references;
    size_t count;
    size_t capacity;
} CoreAsmImmediateSnapshot;

static void core_asm_immediate_snapshot_destroy(CoreAsmImmediateSnapshot *snapshot) {
    size_t i;
    if (snapshot == NULL) return;
    for (i = 0U; i < snapshot->count; ++i) free(snapshot->references[i].name);
    free(snapshot->references);
    (void)memset(snapshot, 0, sizeof(*snapshot));
}

static bool core_asm_immediate_snapshot_append(CoreAsmImmediateSnapshot *snapshot,
        MinicCoreInlineAsmSymbolKind kind, const char *name, size_t length,
        int64_t addend) {
    MinicCoreInlineAsmSymbolReference *next;
    size_t new_capacity, i;
    char *copy;
    if (snapshot == NULL || name == NULL || length == 0U || length == SIZE_MAX)
        return false;
    for (i = 0U; i < snapshot->count; ++i) {
        const MinicCoreInlineAsmSymbolReference *ref = &snapshot->references[i];
        if (ref->kind == kind && ref->name_length == length &&
            ref->byte_addend == addend && memcmp(ref->name, name, length) == 0)
            return true;
    }
    copy = malloc(length + 1U);
    if (copy == NULL) return false;
    (void)memcpy(copy, name, length);
    copy[length] = '\0';
    if (snapshot->count == snapshot->capacity) {
        new_capacity = snapshot->capacity == 0U ? 4U : snapshot->capacity * 2U;
        if (new_capacity < snapshot->capacity ||
            new_capacity > SIZE_MAX / sizeof(*next)) {
            free(copy);
            return false;
        }
        next = realloc(snapshot->references, new_capacity * sizeof(*next));
        if (next == NULL) {
            free(copy);
            return false;
        }
        snapshot->references = next;
        snapshot->capacity = new_capacity;
    }
    snapshot->references[snapshot->count].kind = kind;
    snapshot->references[snapshot->count].name = copy;
    snapshot->references[snapshot->count].name_length = length;
    snapshot->references[snapshot->count].byte_addend = addend;
    ++snapshot->count;
    return true;
}

static bool core_asm_capture_source_expression(CoreAsmSymbolCapture *capture,
                                                MinicExpressionId expression_id);

static bool core_asm_capture_child_expression(MinicExpressionId child, void *context) {
    return core_asm_capture_source_expression((CoreAsmSymbolCapture *)context, child);
}

static bool core_asm_capture_source_expression(CoreAsmSymbolCapture *capture,
                                                MinicExpressionId expression_id) {
    const MinicC0Program *program = capture->lower->body->program;
    const MinicExpression *expression;
    const char *name = NULL;
    size_t name_length = 0U;
    MinicCoreInlineAsmSymbolKind kind = MINIC_CORE_INLINE_ASM_SYMBOL_OBJECT;
    if (++capture->depth > 64U) {
        capture->uninspectable = true;
        --capture->depth;
        return true;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        capture->uninspectable = true;
        --capture->depth;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        const MinicFunction *function = minic_c0_program_function(
            program, expression->value.function_id);
        if (function != NULL) {
            name = function->assembler_name != NULL && function->assembler_name_length != 0U
                ? function->assembler_name : function->name;
            name_length = function->assembler_name != NULL &&
                          function->assembler_name_length != 0U
                ? function->assembler_name_length : function->name_length;
            kind = MINIC_CORE_INLINE_ASM_SYMBOL_FUNCTION;
        }
    } else if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *object = minic_c0_program_global_object(
            program, expression->value.global_object_id);
        if (object != NULL) {
            name = object->name;
            name_length = object->name_length;
        }
    } else if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL ||
               expression->kind == MINIC_EXPRESSION_STATEMENT) {
        /* These expressions can own hidden statements/initializer graphs.
         * Retain a per-live-ASM conservative object root, never a TU root. */
        capture->uninspectable = true;
    }
    if (name != NULL && name_length != 0U &&
        !minic_core_function_add_inline_asm_symbol_reference(
            capture->lower->function, capture->asm_id, kind, name, name_length, 0)) {
        capture->failed = true;
        --capture->depth;
        return false;
    }
    if (!minic_c0_expression_visit_child_ids(
            expression, core_asm_capture_child_expression, capture) && !capture->failed)
        capture->uninspectable = true;
    --capture->depth;
    return !capture->failed;
}

/* Snapshot semantic `i` inputs before handlers potentially revoke a local's
 * entry-specialization fact. Keep this buffer private to the dispatcher;
 * only a successfully emitted Core ASM effect receives owned typed copies. */
static bool core_asm_snapshot_symbolic_fact(const MinicCoreLowerContext *context,
                                            MinicExpressionId expression_id,
                                            CoreAsmImmediateSnapshot *snapshot) {
    const MinicC0Program *program = context->body->program;
    const char *symbol;
    int64_t addend;
    size_t length, i;
    if (!core_inline_asm_symbolic_address_fact(
            context, expression_id, &symbol, &addend)) return true;
    length = strlen(symbol);
    for (i = 0U; i < program->function_count; ++i) {
        const MinicFunction *function = &program->functions[i];
        const char *candidate = function->assembler_name != NULL &&
                                function->assembler_name_length != 0U
                                    ? function->assembler_name : function->name;
        size_t candidate_length = function->assembler_name != NULL &&
                                  function->assembler_name_length != 0U
                                     ? function->assembler_name_length : function->name_length;
        if (candidate != NULL && candidate_length == length &&
            memcmp(candidate, symbol, length) == 0 &&
            !core_asm_immediate_snapshot_append(snapshot,
                MINIC_CORE_INLINE_ASM_SYMBOL_FUNCTION, symbol, length, addend)) return false;
    }
    for (i = 0U; i < program->global_object_count; ++i) {
        const MinicGlobalObject *object = &program->global_objects[i];
        if (object->name != NULL && object->name_length == length &&
            memcmp(object->name, symbol, length) == 0 &&
            !core_asm_immediate_snapshot_append(snapshot,
                MINIC_CORE_INLINE_ASM_SYMBOL_OBJECT, symbol, length, addend)) return false;
    }
    return true;
}

static bool core_asm_snapshot_immediate_inputs(const MinicCoreLowerContext *context,
                                               const MinicInlineAsm *source,
                                               CoreAsmImmediateSnapshot *snapshot) {
    size_t i;
    for (i = 0U; i < source->input_count; ++i) {
        const MinicInlineAsmOperand *operand = &source->inputs[i];
        if (operand->constraint_text != NULL &&
            memchr(operand->constraint_text, 'i', operand->constraint_length) != NULL &&
            !core_asm_snapshot_symbolic_fact(context, operand->expression, snapshot))
            return false;
    }
    return true;
}

static bool core_asm_capture_asm_references(MinicCoreLowerContext *context,
                                             const MinicInlineAsm *source,
                                             MinicCoreInlineAsmId asm_id,
                                             const CoreAsmImmediateSnapshot *snapshot) {
    CoreAsmSymbolCapture capture = {context, asm_id, 0U, false, false};
    size_t i;
    for (i = 0U; i < source->output_count; ++i)
        if (!core_asm_capture_source_expression(&capture, source->outputs[i].expression))
            return false;
    for (i = 0U; i < source->input_count; ++i)
        if (!core_asm_capture_source_expression(
                &capture, source->inputs[i].expression)) return false;
    for (i = 0U; i < snapshot->count; ++i) {
        const MinicCoreInlineAsmSymbolReference *ref = &snapshot->references[i];
        if (!minic_core_function_add_inline_asm_symbol_reference(
                context->function, asm_id, ref->kind, ref->name,
                ref->name_length, ref->byte_addend)) return false;
    }
    if (capture.uninspectable)
        context->function->inline_asms[asm_id].preserve_all_static_objects = true;
    return true;
}

static bool core_asm_instruction_id(const MinicCoreInstruction *ins,
                                    MinicCoreInlineAsmId *asm_id) {
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
        *asm_id = ins->value.inline_asm_id; return true;
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
        *asm_id = ins->value.register_output_input_inline_asm.inline_asm_id; return true;
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
        *asm_id = ins->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id;
        return true;
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        *asm_id = ins->value.scalar_input_inline_asm.inline_asm_id; return true;
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        *asm_id = ins->value.structured_inline_asm.inline_asm_id; return true;
    default: return false;
    }
}

static MinicCoreLowerStatus core_asm_finish_claim(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, MinicCoreLowerStatus status,
    size_t before_instructions, size_t before_asms,
    CoreAsmImmediateSnapshot *snapshot, const char *reason) {
    size_t i;
    if (status == MINIC_CORE_LOWER_OK) {
        for (i = before_instructions; i < context->function->instruction_count; ++i) {
            const MinicCoreInstruction *ins = &context->function->instructions[i];
            MinicCoreInlineAsmId asm_id;
            if (ins->span.begin.offset != statement->span.begin.offset ||
                ins->span.end.offset != statement->span.end.offset ||
                !core_asm_instruction_id(ins, &asm_id) || asm_id < before_asms)
                continue;
            if (asm_id >= context->function->inline_asm_count ||
                !core_asm_capture_asm_references(context, source, asm_id, snapshot)) {
                status = MINIC_CORE_LOWER_ERROR;
                break;
            }
        }
    }
    core_asm_immediate_snapshot_destroy(snapshot);
    return core_asm_trace_status(context, source, status, reason);
}

MinicCoreLowerStatus minic_core_lower_inline_asm(MinicCoreLowerContext *context,
                                                    const MinicStatement *statement) {
    const MinicInlineAsm *source;
    MinicCoreInlineAsmId inline_asm_id;
    MinicCoreInstruction instruction;
    MinicType instruction_source_type = minic_type_void();
    MinicCoreLowerStatus status;
    size_t before_instructions, before_asms;
    CoreAsmImmediateSnapshot pending_immediates = {0};
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
    before_instructions = context->function->instruction_count;
    before_asms = context->function->inline_asm_count;

    /* GNU allows more operands than our fixed-size Core representation.
     * Reject the entire shape before any target or legacy lowering can claim
     * it, including the overflow-safe mixed output/input boundary. */
    if (source->output_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT ||
        source->input_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT -
                                  source->output_count) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }


    core_asm_trace_input_batch(context, source);
    if (!core_asm_snapshot_immediate_inputs(context, source, &pending_immediates)) {
        core_asm_immediate_snapshot_destroy(&pending_immediates);
        return MINIC_CORE_LOWER_ERROR;
    }

    /* Target-selected GNU register constraints still transport a canonical
     * structured Core effect. Preflight does not mutate on an unsupported
     * shape; other targets retain the original ordered lowering. */
    handled = false;
    status = core_asm_try_target_register_contract(context, statement, source, &handled);
    if (handled) return core_asm_finish_claim(context, statement, source, status,
                            before_instructions, before_asms, &pending_immediates, "target_lower");
    /* A target-qualified instruction with GNU input operands must never
     * fall back to Core's general scalar/opaque inline-asm machinery after
     * its target rejects the specific constraints or instruction suffix.
     * This is one shared ownership gate, with ISA mnemonics supplied solely
     * by each backend descriptor (not Frontend/Core arch conditionals). */
    if (minic_source_backend_requires_qualified_input_asm(
            minic_source_backend_for_target(&context->target->machine), source)) {
        core_asm_trace_frontier(context, source, "target_required");
        core_asm_immediate_snapshot_destroy(&pending_immediates);
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    handled = false;
    status = core_asm_try_immediate_output_csr(context, statement, source, &handled);
    if (handled) return core_asm_finish_claim(context, statement, source, status,
                            before_instructions, before_asms, &pending_immediates, "immediate_output");

    handled = false;
    status = core_asm_try_structured_roles_and_asm_goto(context, statement, source, &handled);
    if (handled) return core_asm_finish_claim(context, statement, source, status,
                            before_instructions, before_asms, &pending_immediates, "structured_roles");

    handled = false;
    status = core_asm_try_atomic_and_immediate_forms(context, statement, source, &handled);
    if (handled) return core_asm_finish_claim(context, statement, source, status,
                            before_instructions, before_asms, &pending_immediates, "atomic_immediate");

    handled = false;
    status = core_asm_try_mixed_memory_forms(context, statement, source, &handled);
    if (handled) return core_asm_finish_claim(context, statement, source, status,
                            before_instructions, before_asms, &pending_immediates, "mixed_memory");

    handled = false;
    status = core_asm_try_readwrite_and_output_specialization(context, statement, source, &handled);
    if (handled) return core_asm_finish_claim(context, statement, source, status,
                            before_instructions, before_asms, &pending_immediates, "readwrite_output");

    handled = false;
    status = core_asm_try_empty_barrier_scalar_forms(context, statement, source, &handled);
    if (handled) return core_asm_finish_claim(context, statement, source, status,
                            before_instructions, before_asms, &pending_immediates, "empty_barrier");

    core_asm_trace_unclaimed(context, source);

    if (!source->is_volatile || source->is_goto || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 0U || source->input_count != 0U ||
        source->label_count != 0U || source->register_clobber_count != 0U) {
        core_asm_trace_frontier(context, source, "no_handler");
        core_asm_immediate_snapshot_destroy(&pending_immediates);
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                   source->template_text,
                                                   source->template_length,
                                                   source->is_volatile,
                                                   source->has_memory_clobber,
                                                   &inline_asm_id)) {
        core_asm_immediate_snapshot_destroy(&pending_immediates);
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
    instruction.span = statement->span;
    instruction_source_type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.inline_asm_id = inline_asm_id;
    status = minic_core_function_append_source_effect_instruction(
               context->source, context->block_id, &instruction, instruction_source_type)
               ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    return core_asm_finish_claim(context, statement, source, status,
                             before_instructions, before_asms, &pending_immediates, "opaque");
}
