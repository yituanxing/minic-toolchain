#!/usr/bin/env python3
from pathlib import Path

p = Path('src/core/core_lower.c')
text = p.read_text()
old = '''    bool pointer_comparison;\n\n    if (context == NULL || context->body == NULL || context->body->program == NULL ||\n        context->function == NULL || left_value == NULL || right_value == NULL) {\n'''
new = '''    bool integer_comparison;\n    bool pointer_comparison;\n\n    if (context == NULL || context->body == NULL || context->body->program == NULL ||\n        context->function == NULL || left_value == NULL || right_value == NULL) {\n'''
if text.count(old) != 1:
    raise SystemExit('declaration anchor mismatch')
text = text.replace(old, new, 1)

old = '''    pointer_comparison = false;\n    if (minic_type_is_integer(left_type) && minic_type_is_integer(right_type)) {\n        if (!minic_type_equal(left_type, right_type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        comparison_type = left_type;\n    } else if (minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type)) {\n'''
new = '''    integer_comparison = false;\n    pointer_comparison = false;\n    if (minic_type_is_integer(left_type) && minic_type_is_integer(right_type)) {\n        if (context->target == NULL ||\n            !minic_target_info_integer_common_for_program(context->target,\n                                                          context->body->program,\n                                                          left_type,\n                                                          right_type,\n                                                          &comparison_type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        integer_comparison = true;\n    } else if (minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type)) {\n'''
if text.count(old) != 1:
    raise SystemExit('common-type anchor mismatch')
text = text.replace(old, new, 1)

old = '''    if (pointer_comparison) {\n        status = append_scalar_bitcast(\n            context, left_expression->span, comparison_type, left_source, &left_normalized);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n    } else {\n        left_normalized = left_source;\n    }\n'''
new = '''    if (integer_comparison) {\n        status = append_integer_conversion(\n            context, left_expression->span, comparison_type, left_source, &left_normalized);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n    } else if (pointer_comparison) {\n        status = append_scalar_bitcast(\n            context, left_expression->span, comparison_type, left_source, &left_normalized);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n    } else {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n'''
if text.count(old) != 1:
    raise SystemExit('left-normalize anchor mismatch')
text = text.replace(old, new, 1)

old = '''    if (pointer_comparison) {\n        status = append_scalar_bitcast(\n            context, right_expression->span, comparison_type, right_source, &right_normalized);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n    } else {\n        right_normalized = right_source;\n    }\n'''
new = '''    if (integer_comparison) {\n        status = append_integer_conversion(\n            context, right_expression->span, comparison_type, right_source, &right_normalized);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n    } else if (pointer_comparison) {\n        status = append_scalar_bitcast(\n            context, right_expression->span, comparison_type, right_source, &right_normalized);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n    } else {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n'''
if text.count(old) != 1:
    raise SystemExit('right-normalize anchor mismatch')
text = text.replace(old, new, 1)

p.write_text(text)
