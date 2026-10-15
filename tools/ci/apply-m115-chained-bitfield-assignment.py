#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
marker = "M115_CHAINED_BIT_FIELD_ASSIGNMENT"
if marker in text:
    raise SystemExit("M115 already applied")

if text.count("lower_assignment_pair(") != 3:
    raise SystemExit(f"unexpected lower_assignment_pair count: {text.count('lower_assignment_pair(')}")

prototype_anchor = """static MinicCoreLowerStatus lower_expression(MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicCoreValueId *value_id);
"""
prototype_insert = prototype_anchor + """static MinicCoreLowerStatus lower_assignment_pair(MinicCoreLowerContext *context,
                                                  MinicExpressionId target_id,
                                                  MinicExpressionId source_id,
                                                  MinicSourceSpan span,
                                                  MinicCoreValueId *result_value);
"""
if text.count(prototype_anchor) != 1:
    raise SystemExit("lower_expression prototype anchor mismatch")
text = text.replace(prototype_anchor, prototype_insert, 1)

m65_begin = text.index("    /* M65_SCALAR_ASSIGNMENT_EXPRESSION_VALUE:")
m65_end = text.index("    if (expression->kind == MINIC_EXPRESSION_DISCARD) {", m65_begin)
m65 = """    /* M115_CHAINED_BIT_FIELD_ASSIGNMENT: a simple scalar assignment has one
       lowering owner whether its value is discarded by statement context or
       consumed by a surrounding expression.  Reuse lower_assignment_pair so
       addressable scalars and unsigned bit-fields share exactly the same store
       semantics; expression context additionally receives the value actually
       stored after destination conversion/bit-field truncation. */
    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT) {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicType expression_value_type;
        MinicType stored_type;

        target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        source = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (target == NULL || source == NULL ||
            target->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(target->type) ||
            !minic_type_unqualified(target->type, &stored_type) ||
            !core_memory_scalar_type(stored_type) ||
            !minic_type_unqualified(expression->type, &expression_value_type) ||
            !minic_type_equal(expression_value_type, stored_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return lower_assignment_pair(context,
                                     expression->value.binary.left,
                                     expression->value.binary.right,
                                     expression->span,
                                     value_id);
    }
"""
text = text[:m65_begin] + m65 + text[m65_end:]

func_begin = text.index("static MinicCoreLowerStatus lower_assignment_pair(MinicCoreLowerContext *context,", text.index("static MinicCoreLowerStatus lower_expression("))
# Skip the newly inserted forward declaration and find the definition after lower_expression.
func_begin = text.index("static MinicCoreLowerStatus lower_assignment_pair(MinicCoreLowerContext *context,", func_begin + 1)
func_end = text.index("\nstatic MinicCoreLowerStatus lower_assignment(", func_begin)
func = text[func_begin:func_end]

old_sig = """static MinicCoreLowerStatus lower_assignment_pair(MinicCoreLowerContext *context,
                                                  MinicExpressionId target_id,
                                                  MinicExpressionId source_id,
                                                  MinicSourceSpan span) {
"""
new_sig = """static MinicCoreLowerStatus lower_assignment_pair(MinicCoreLowerContext *context,
                                                  MinicExpressionId target_id,
                                                  MinicExpressionId source_id,
                                                  MinicSourceSpan span,
                                                  MinicCoreValueId *result_value) {
"""
if func.count(old_sig) != 1:
    raise SystemExit("assignment-pair signature mismatch")
func = func.replace(old_sig, new_sig, 1)

old_decls = """            MinicCoreValueId current;
            MinicCoreValueId field_value;
            MinicCoreValueId constant;
            MinicCoreValueId merged;
"""
new_decls = """            MinicCoreValueId current;
            MinicCoreValueId field_value;
            MinicCoreValueId field_storage;
            MinicCoreValueId assigned_value;
            MinicCoreValueId constant;
            MinicCoreValueId merged;
"""
if func.count(old_decls) != 1:
    raise SystemExit("bit-field value declarations mismatch")
func = func.replace(old_decls, new_decls, 1)

old_convert = """            if (!minic_type_equal(storage_type, value_type)) {
                bit_status = append_integer_conversion(
                    context, span, storage_type, field_value, &field_value);
                if (bit_status != MINIC_CORE_LOWER_OK) {
                    return bit_status;
                }
            }

            low_mask = field->bit_width == 64U
"""
new_convert = """            if (minic_type_equal(storage_type, value_type)) {
                field_storage = field_value;
            } else {
                bit_status = append_integer_conversion(
                    context, span, storage_type, field_value, &field_storage);
                if (bit_status != MINIC_CORE_LOWER_OK) {
                    return bit_status;
                }
            }

            low_mask = field->bit_width == 64U
"""
if func.count(old_convert) != 1:
    raise SystemExit("bit-field storage conversion anchor mismatch")
func = func.replace(old_convert, new_convert, 1)

old_mask = """                operation.value.binary.left = field_value;
                operation.value.binary.right = constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &field_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (bit_offset != 0U) {
"""
new_mask = """                operation.value.binary.left = field_storage;
                operation.value.binary.right = constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &field_storage)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (minic_type_equal(storage_type, value_type)) {
                assigned_value = field_storage;
            } else {
                bit_status = append_integer_conversion(
                    context, span, value_type, field_storage, &assigned_value);
                if (bit_status != MINIC_CORE_LOWER_OK) {
                    return bit_status;
                }
            }
            if (bit_offset != 0U) {
"""
if func.count(old_mask) != 1:
    raise SystemExit("bit-field mask anchor mismatch")
func = func.replace(old_mask, new_mask, 1)

old_shift = """                operation.value.binary.left = field_value;
                operation.value.binary.right = constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &field_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }

            field_mask = low_mask << bit_offset;
"""
new_shift = """                operation.value.binary.left = field_storage;
                operation.value.binary.right = constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &field_storage)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }

            field_mask = low_mask << bit_offset;
"""
if func.count(old_shift) != 1:
    raise SystemExit("bit-field shift anchor mismatch")
func = func.replace(old_shift, new_shift, 1)

old_merge = """            operation.value.binary.left = merged;
            operation.value.binary.right = field_value;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &operation, &merged)) {
"""
new_merge = """            operation.value.binary.left = merged;
            operation.value.binary.right = field_storage;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &operation, &merged)) {
"""
if func.count(old_merge) != 1:
    raise SystemExit("bit-field merge anchor mismatch")
func = func.replace(old_merge, new_merge, 1)

old_bit_tail = """            operation.value.store.stored_value = merged;
            operation.value.store.is_volatile = minic_type_is_volatile(target->type);
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &operation)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
"""
new_bit_tail = """            operation.value.store.stored_value = merged;
            operation.value.store.is_volatile = minic_type_is_volatile(target->type);
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &operation)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (result_value != NULL) {
                *result_value = assigned_value;
            }
            return MINIC_CORE_LOWER_OK;
"""
if func.count(old_bit_tail) != 1:
    raise SystemExit("bit-field store tail mismatch")
func = func.replace(old_bit_tail, new_bit_tail, 1)

old_scalar_tail = """        return MINIC_CORE_LOWER_ERROR;
    }
    return MINIC_CORE_LOWER_OK;
}
"""
new_scalar_tail = """        return MINIC_CORE_LOWER_ERROR;
    }
    if (result_value != NULL) {
        *result_value = stored_value;
    }
    return MINIC_CORE_LOWER_OK;
}
"""
if func.count(old_scalar_tail) != 1:
    raise SystemExit("ordinary assignment tail mismatch")
func = func.replace(old_scalar_tail, new_scalar_tail, 1)

text = text[:func_begin] + func + text[func_end:]

old_statement_call = "return lower_assignment_pair(context, target_id, source_id, statement->span);"
if text.count(old_statement_call) != 1:
    raise SystemExit("statement assignment call mismatch")
text = text.replace(old_statement_call,
                    "return lower_assignment_pair(context, target_id, source_id, statement->span, NULL);",
                    1)

old_expression_statement_call = "return lower_assignment_pair(context, target_id, source_id, expression->span);"
if text.count(old_expression_statement_call) != 1:
    raise SystemExit("expression-statement assignment call mismatch")
text = text.replace(old_expression_statement_call,
                    "return lower_assignment_pair(context, target_id, source_id, expression->span, NULL);",
                    1)

if text.count(marker) != 1:
    raise SystemExit(f"expected one M115 marker, got {text.count(marker)}")
if text.count("lower_assignment_pair(") != 5:
    raise SystemExit(f"unexpected final lower_assignment_pair count: {text.count('lower_assignment_pair(')}")
path.write_text(text)
