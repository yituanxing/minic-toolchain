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

# M117_BLOCK_LOCAL_POINTER_RELATIONAL: Core intentionally uses block-local SSA.
# A pointer relational operand may itself contain a conditional or another
# expression that creates CFG. Preserve the normalized left value in a Core
# object before lowering the right operand, then reload it in the final compare
# block. This mirrors the existing block-local discipline used by calls,
# assignments, integer binary operands, and pointer difference lowering.
left_anchor = """            if (!minic_type_equal(context->function->values[left].type, common_type)) {
                status = append_scalar_bitcast(
                    context, left_expression->span, common_type, left, &left);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            status = lower_expression(context, expression->value.binary.right, &right);
"""
left_insert = """            if (!minic_type_equal(context->function->values[left].type, common_type)) {
                status = append_scalar_bitcast(
                    context, left_expression->span, common_type, left, &left);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            /* M117_BLOCK_LOCAL_POINTER_RELATIONAL: lowering the right operand
               may create a new Core block. Spill the normalized left pointer so
               the eventual POINTER_LESS never references an SSA value owned by
               a predecessor block. */
            MinicCoreObjectId left_object;
            status = spill_scalar_value(
                context, left_expression->span, common_type, left, &left_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = lower_expression(context, expression->value.binary.right, &right);
"""
if text.count(left_anchor) != 1:
    raise SystemExit(f"pointer relational left anchor mismatch: {text.count(left_anchor)}")
text = text.replace(left_anchor, left_insert, 1)

right_anchor = """            if (!minic_type_equal(context->function->values[right].type, common_type)) {
                status = append_scalar_bitcast(
                    context, right_expression->span, common_type, right, &right);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            swap = expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
"""
right_insert = """            if (!minic_type_equal(context->function->values[right].type, common_type)) {
                status = append_scalar_bitcast(
                    context, right_expression->span, common_type, right, &right);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            /* M117_BLOCK_LOCAL_POINTER_RELATIONAL: reload only after the right
               operand is fully lowered and normalized, in the final comparison
               block. */
            status = reload_scalar_value(
                context, left_expression->span, common_type, left_object, &left);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            swap = expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
"""
if text.count(right_anchor) != 1:
    raise SystemExit(f"pointer relational right anchor mismatch: {text.count(right_anchor)}")
text = text.replace(right_anchor, right_insert, 1)

if text.count(marker) != 3:
    raise SystemExit(f"expected three M116 markers, got {text.count(marker)}")
if text.count("M117_BLOCK_LOCAL_POINTER_RELATIONAL") != 2:
    raise SystemExit(
        f"expected two M117 markers, got {text.count('M117_BLOCK_LOCAL_POINTER_RELATIONAL')}"
    )
path.write_text(text)
