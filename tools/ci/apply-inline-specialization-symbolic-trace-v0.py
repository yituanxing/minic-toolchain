#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower_asm.c")
text = p.read_text()

array_old = r'''    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base =
            minic_c0_program_expression(program, expression->value.subscript.base);
        MinicType element_type;
        size_t element_size;
        size_t element_alignment;
        int64_t index;
        int64_t base_addend;
        int64_t scaled;

        if (base == NULL || !minic_type_is_pointer(base->type) ||
            !minic_type_pointee(base->type, &element_type) ||
            !minic_data_layout_type(
                minic_target_info_data_layout(context->target),
                program,
                element_type,
                &element_size,
                &element_alignment) ||
            element_size > (size_t)INT64_MAX ||
            !core_inline_asm_specialized_integer(
                context, expression->value.subscript.index, &index, depth + 1U) ||
            !core_inline_asm_symbolic_address_depth(
                context,
                expression->value.subscript.base,
                symbol,
                &base_addend,
                depth + 1U)) {
            return false;
        }
        if (index != 0 &&
            ((index > 0 && index > INT64_MAX / (int64_t)element_size) ||
             (index < 0 && index < INT64_MIN / (int64_t)element_size))) {
            return false;
        }
        scaled = index * (int64_t)element_size;
        if ((scaled > 0 && base_addend > INT64_MAX - scaled) ||
            (scaled < 0 && base_addend < INT64_MIN - scaled)) {
            return false;
        }
        *addend = base_addend + scaled;
        return true;
    }
'''
array_new = r'''    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base =
            minic_c0_program_expression(program, expression->value.subscript.base);
        const MinicArrayType *array_type = NULL;
        MinicType element_type;
        size_t element_size;
        size_t element_alignment;
        int64_t index;
        int64_t base_addend;
        int64_t scaled;
        bool base_ok;

        if (base == NULL) {
            return false;
        }
        if (minic_type_is_pointer(base->type)) {
            if (!minic_type_pointee(base->type, &element_type)) {
                return false;
            }
            base_ok = core_inline_asm_symbolic_address_depth(
                context,
                expression->value.subscript.base,
                symbol,
                &base_addend,
                depth + 1U);
        } else if (minic_type_is_array(base->type)) {
            array_type = minic_c0_program_array_type(program, base->type.array_type_id);
            if (array_type == NULL) {
                return false;
            }
            element_type = array_type->element_type;
            base_ok = core_inline_asm_symbolic_lvalue_address(
                context,
                expression->value.subscript.base,
                symbol,
                &base_addend,
                depth + 1U);
        } else {
            return false;
        }
        if (!base_ok ||
            !minic_data_layout_type(
                minic_target_info_data_layout(context->target),
                program,
                element_type,
                &element_size,
                &element_alignment) ||
            element_size > (size_t)INT64_MAX ||
            !core_inline_asm_specialized_integer(
                context, expression->value.subscript.index, &index, depth + 1U)) {
            return false;
        }
        if (index != 0 &&
            ((index > 0 && index > INT64_MAX / (int64_t)element_size) ||
             (index < 0 && index < INT64_MIN / (int64_t)element_size))) {
            return false;
        }
        scaled = index * (int64_t)element_size;
        if ((scaled > 0 && base_addend > INT64_MAX - scaled) ||
            (scaled < 0 && base_addend < INT64_MIN - scaled)) {
            return false;
        }
        *addend = base_addend + scaled;
        return true;
    }
'''
if text.count(array_old) != 1:
    raise SystemExit(f"expected one symbolic subscript resolver, found {text.count(array_old)}")
text = text.replace(array_old, array_new, 1)

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
