#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()

old = '''    /* M106_MATERIALIZED_LOCAL_ARRAY_OBJECT: frontend array convergence has
       two local-object forms. Legacy locals keep element type + is_array/count;
       typedef/materialized locals carry one complete array MinicType directly.
       A materialized array is one Core object whose DataLayout already owns the
       full extent, so its address is naturally pointer-to-array. */
    if (minic_type_is_array(local->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(
            context->body->program, local->type.array_type_id);
        if (local->is_array || array_type == NULL || array_type->element_count == 0U ||
            array_type->is_zero_length) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_core_function_add_object(
                context->function, local->name_span, local->type, object_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        context->local_objects[local_index] = *object_id;
        return MINIC_CORE_LOWER_OK;
    }
'''

new = '''    /* M106_MATERIALIZED_LOCAL_ARRAY_OBJECT: frontend array convergence has
       two local-object forms. Legacy locals keep element type + is_array/count;
       typedef/materialized locals carry one complete array MinicType directly.
       A materialized array is one Core object whose DataLayout already owns the
       full extent, so its address is naturally pointer-to-array. */
    /* M175A_REPEATED_ARRAY_OBJECT: an outer legacy array may itself have a
       materialized array element type (for example `typedef int Row[3];
       Row rows[2];`).  In that mixed representation local->type describes one
       complete element object and local->element_count describes the outer
       repetition.  Preserve both dimensions by using Core's repeated-object
       form instead of rejecting local->is_array. */
    if (minic_type_is_array(local->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(
            context->body->program, local->type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U ||
            array_type->is_zero_length) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (local->is_array) {
            if (local->element_count == 0U ||
                !minic_core_function_add_repeated_object(context->function,
                                                         local->name_span,
                                                         local->type,
                                                         local->element_count,
                                                         object_id)) {
                return MINIC_CORE_LOWER_ERROR;
            }
        } else if (!minic_core_function_add_object(
                       context->function, local->name_span, local->type, object_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        context->local_objects[local_index] = *object_id;
        return MINIC_CORE_LOWER_OK;
    }
'''

if text.count(old) != 1:
    raise SystemExit(f"M175A lower_local_object anchor count={text.count(old)}")

path.write_text(text.replace(old, new, 1))
