#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower_asm.c")
text = p.read_text()

old = r'''    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
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
new = r'''    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
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
            if (array_type == NULL || array_type->element_count == 0U ||
                array_type->is_zero_length) {
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

count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one symbolic lvalue subscript block, found {count}")
text = text.replace(old, new, 1)
p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_ARRAY_LVALUE_V0=APPLIED")
