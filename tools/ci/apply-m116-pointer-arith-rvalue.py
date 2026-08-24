#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
marker = "M116_POINTER_ARITH_RVALUE_TYPE"
if marker in text:
    raise SystemExit("M116 already applied")

helper_anchor = """    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        return minic_type_unqualified(expression->type, value_type);
    }
    if (expression->value_category == MINIC_VALUE_LVALUE) {
"""
helper_insert = """    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        return minic_type_unqualified(expression->type, value_type);
    }
    /* M116_POINTER_ARITH_RVALUE_TYPE: pointer-valued +/- is a transported
       scalar value just like a conditional/conversion result.  The semantic AST
       may retain a top-level qualifier inherited from the source object, but
       every Core consumer (calls, returns, nested arithmetic, stores) must see
       the same unqualified rvalue type.  Keep pointee qualifiers intact. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        minic_type_is_pointer(expression->type) &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        return minic_type_unqualified(expression->type, value_type);
    }
    if (expression->value_category == MINIC_VALUE_LVALUE) {
"""
if text.count(helper_anchor) != 1:
    raise SystemExit(f"scalar value-type helper anchor mismatch: {text.count(helper_anchor)}")
text = text.replace(helper_anchor, helper_insert, 1)

old_decl = """        MinicType expression_value_type;
        MinicType pointer_value_type;
        MinicType index_value_type;
"""
new_decl = """        MinicType expression_value_type;
        MinicType pointer_source_type;
        MinicType pointer_value_type;
        MinicType index_value_type;
"""
if text.count(old_decl) != 1:
    raise SystemExit(f"M82 declaration anchor mismatch: {text.count(old_decl)}")
text = text.replace(old_decl, new_decl, 1)

old_check = """        if (!core_scalar_expression_value_type(
                context->body, pointer_expression, &pointer_value_type) ||
            !core_scalar_expression_value_type(
                context->body, index_expression, &index_value_type) ||
            !minic_type_is_pointer(pointer_value_type) ||
"""
new_check = """        /* M116_POINTER_ARITH_RVALUE_TYPE: pointer arithmetic consumes and
           produces C scalar values.  A nested arithmetic expression may retain
           a top-level qualifier in the semantic AST spelling, but that qualifier
           belongs to the source object, not the transported rvalue.  Canonicalize
           only the pointer operand's top-level qualifier here; pointee qualifiers
           remain part of the pointer type. */
        if (!core_scalar_expression_value_type(
                context->body, pointer_expression, &pointer_source_type) ||
            !minic_type_unqualified(pointer_source_type, &pointer_value_type) ||
            !core_scalar_expression_value_type(
                context->body, index_expression, &index_value_type) ||
            !minic_type_is_pointer(pointer_value_type) ||
"""
if text.count(old_check) != 1:
    raise SystemExit(f"M82 value-type anchor mismatch: {text.count(old_check)}")
text = text.replace(old_check, new_check, 1)

old_tail = """        /* Pointer arithmetic computes in the lvalue-to-rvalue pointer type.
           If the semantic expression retains a top-level pointer qualifier,
           represent that result spelling with Core's pointer bitcast rather
           than making POINTER_OFFSET violate its base/result type invariant. */
        instruction.value.pointer_offset.subtract =
            expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &offset_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_equal(pointer_value_type, expression->type)) {
            *value_id = offset_value;
            return MINIC_CORE_LOWER_OK;
        }
        return append_scalar_bitcast(
            context, expression->span, expression->type, offset_value, value_id);
"""
new_tail = """        /* M116_POINTER_ARITH_RVALUE_TYPE: POINTER_OFFSET already has the
           canonical lvalue-to-rvalue pointer type.  Do not re-attach a top-level
           qualifier merely because the AST keeps that source spelling; nested
           pointer arithmetic and return/assignment conversion consume the value
           type, not the storage-qualified spelling. */
        instruction.value.pointer_offset.subtract =
            expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &offset_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = offset_value;
        return MINIC_CORE_LOWER_OK;
"""
if text.count(old_tail) != 1:
    raise SystemExit(f"M82 result tail anchor mismatch: {text.count(old_tail)}")
text = text.replace(old_tail, new_tail, 1)

if text.count(marker) != 3:
    raise SystemExit(f"expected three M116 markers, got {text.count(marker)}")
path.write_text(text)
