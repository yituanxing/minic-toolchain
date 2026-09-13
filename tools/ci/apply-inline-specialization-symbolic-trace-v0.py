#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower_asm.c")
text = p.read_text()

anchor = '''static bool core_inline_asm_specialized_symbolic_text(
'''
helper = r'''static void core_inline_asm_trace_symbolic_expression(
    const MinicC0Program *program,
    MinicExpressionId expression_id,
    unsigned int depth) {
    const MinicExpression *expression;

    if (program == NULL || depth > 12U) {
        return;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        (void)fprintf(stderr,
                      "INLINE_SPEC_SYMBOLIC_EXPR depth=%u id=%zu missing=1",
                      depth,
                      (size_t)expression_id);
        (void)fputc(10, stderr);
        return;
    }
    (void)fprintf(stderr,
                  "INLINE_SPEC_SYMBOLIC_EXPR depth=%u id=%zu kind=%d value_category=%d",
                  depth,
                  (size_t)expression_id,
                  (int)expression->kind,
                  (int)expression->value_category);
    (void)fputc(10, stderr);
    switch (expression->kind) {
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_LVALUE_READ:
    case MINIC_EXPRESSION_ADDRESS_OF:
    case MINIC_EXPRESSION_DEREFERENCE:
        core_inline_asm_trace_symbolic_expression(
            program, expression->value.unary.operand, depth + 1U);
        break;
    case MINIC_EXPRESSION_MEMBER:
        (void)fprintf(stderr,
                      "INLINE_SPEC_SYMBOLIC_MEMBER depth=%u record=%zu field=%zu base=%zu",
                      depth,
                      (size_t)expression->value.member.record_id,
                      expression->value.member.field_index,
                      (size_t)expression->value.member.base);
        (void)fputc(10, stderr);
        core_inline_asm_trace_symbolic_expression(
            program, expression->value.member.base, depth + 1U);
        break;
    case MINIC_EXPRESSION_SUBSCRIPT:
        (void)fprintf(stderr,
                      "INLINE_SPEC_SYMBOLIC_SUBSCRIPT depth=%u base=%zu index=%zu",
                      depth,
                      (size_t)expression->value.subscript.base,
                      (size_t)expression->value.subscript.index);
        (void)fputc(10, stderr);
        core_inline_asm_trace_symbolic_expression(
            program, expression->value.subscript.base, depth + 1U);
        core_inline_asm_trace_symbolic_expression(
            program, expression->value.subscript.index, depth + 1U);
        break;
    case MINIC_EXPRESSION_BINARY:
        (void)fprintf(stderr,
                      "INLINE_SPEC_SYMBOLIC_BINARY depth=%u op=%d left=%zu right=%zu",
                      depth,
                      (int)expression->value.binary.operator_kind,
                      (size_t)expression->value.binary.left,
                      (size_t)expression->value.binary.right);
        (void)fputc(10, stderr);
        core_inline_asm_trace_symbolic_expression(
            program, expression->value.binary.left, depth + 1U);
        core_inline_asm_trace_symbolic_expression(
            program, expression->value.binary.right, depth + 1U);
        break;
    case MINIC_EXPRESSION_LOCAL:
        (void)fprintf(stderr,
                      "INLINE_SPEC_SYMBOLIC_LOCAL depth=%u local=%zu",
                      depth,
                      (size_t)expression->value.local_id);
        (void)fputc(10, stderr);
        break;
    case MINIC_EXPRESSION_GLOBAL_OBJECT:
        (void)fprintf(stderr,
                      "INLINE_SPEC_SYMBOLIC_GLOBAL depth=%u global=%zu",
                      depth,
                      (size_t)expression->value.global_object_id);
        (void)fputc(10, stderr);
        break;
    default:
        break;
    }
}

'''
if text.count(anchor) != 1:
    raise SystemExit("expected one symbolic text anchor")
text = text.replace(anchor, helper + anchor, 1)

old = '''    if (buffer == NULL || capacity == 0U || text_out == NULL || length_out == NULL ||
        !core_inline_asm_symbolic_address_depth(
            context, expression_id, &symbol, &addend, 0U)) {
        return false;
    }
'''
new = r'''    if (buffer == NULL || capacity == 0U || text_out == NULL || length_out == NULL) {
        return false;
    }
    if (!core_inline_asm_symbolic_address_depth(
            context, expression_id, &symbol, &addend, 0U)) {
        if (getenv("MINIC_INLINE_SPEC_SYMBOLIC_TRACE") != NULL &&
            context != NULL && context->body != NULL &&
            context->body->program != NULL && context->source_function != NULL &&
            context->source_function->is_integer_specialization) {
            size_t parameter_index;
            (void)fprintf(stderr,
                          "INLINE_SPEC_SYMBOLIC_RESOLVE_FAIL function=%s source=%zu operand=%zu",
                          context->source_function->name != NULL
                              ? context->source_function->name
                              : "?",
                          (size_t)context->source_function->specialization_source,
                          (size_t)expression_id);
            (void)fputc(10, stderr);
            core_inline_asm_trace_symbolic_expression(
                context->body->program, expression_id, 0U);
            for (parameter_index = 0U;
                 parameter_index < context->source_function->parameter_count;
                 ++parameter_index) {
                (void)fprintf(stderr,
                              "INLINE_SPEC_SYMBOLIC_PARAM index=%zu ik=%d ib=%" PRIu64 " sk=%d se=%zu",
                              parameter_index,
                              context->source_function->specialization_integer_known[parameter_index] ? 1 : 0,
                              context->source_function->specialization_integer_bits[parameter_index],
                              context->source_function->specialization_symbolic_known[parameter_index] ? 1 : 0,
                              (size_t)context->source_function->specialization_symbolic_expression[parameter_index]);
                (void)fputc(10, stderr);
                if (context->source_function->specialization_symbolic_known[parameter_index]) {
                    core_inline_asm_trace_symbolic_expression(
                        context->body->program,
                        context->source_function->specialization_symbolic_expression[parameter_index],
                        1U);
                }
            }
        }
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one symbolic resolve guard, found {text.count(old)}")
text = text.replace(old, new, 1)

p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_TRACE_V0=APPLIED")
