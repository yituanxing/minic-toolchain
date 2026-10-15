#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M31b fixup {label} anchor count={count}, expected 1")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/core/core_lower.c",
    '''static MinicCoreLowerStatus reload_scalar_value(MinicCoreLowerContext *context,\n                                                MinicSourceSpan span,\n                                                MinicType type,\n                                                MinicCoreObjectId object_id,\n                                                MinicCoreValueId *value_id);\n''',
    '''static MinicCoreLowerStatus reload_scalar_value(MinicCoreLowerContext *context,\n                                                MinicSourceSpan span,\n                                                MinicType type,\n                                                MinicCoreObjectId object_id,\n                                                MinicCoreValueId *value_id);\nstatic MinicCoreLowerStatus append_scalar_bitcast(MinicCoreLowerContext *context,\n                                                  MinicSourceSpan span,\n                                                  MinicType target_type,\n                                                  MinicCoreValueId source_value,\n                                                  MinicCoreValueId *value_id);\n''',
    "declaration",
)

replace_once(
    "src/core/core_ir.c",
    '''        if (global->name == NULL || global->name_length == 0U ||\n            (!minic_type_is_integer(global->type) && !minic_type_is_pointer(global->type))) {\n            return false;\n        }\n''',
    '''        if (global->name == NULL || global->name_length == 0U ||\n            (!minic_type_is_integer(global->type) && !minic_type_is_pointer(global->type) &&\n             !minic_type_is_array(global->type))) {\n            return false;\n        }\n''',
    "global verifier",
)

replace_once(
    "src/core/core_ir.c",
    '''        for (parameter_index = 0U; parameter_index < callee->parameter_count; ++parameter_index) {\n            if (!core_call_scalar_type(callee->parameter_types[parameter_index])) {\n                return false;\n            }\n        }\n''',
    '''        for (parameter_index = 0U; parameter_index < callee->parameter_count; ++parameter_index) {\n            if (!core_call_parameter_type(callee->parameter_types[parameter_index])) {\n                return false;\n            }\n        }\n''',
    "callee verifier",
)

replace_once(
    "src/frontend/ast.c",
    '''    (void)memset(&resolved, 0, sizeof(resolved));\n    if (expression->kind == MINIC_EXPRESSION_LOCAL) {\n''',
    '''    (void)memset(&resolved, 0, sizeof(resolved));\n    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {\n        const MinicGlobalObject *object;\n        const MinicArrayType *array_type;\n\n        object = minic_c0_program_global_object(program, expression->value.global_object_id);\n        if (object == NULL || !minic_type_is_array(object->type)) {\n            return false;\n        }\n        array_type = minic_c0_program_array_type(program, object->type.array_type_id);\n        if (array_type == NULL) {\n            return false;\n        }\n        resolved.element_type = array_type->element_type;\n        resolved.element_count = array_type->element_count;\n        resolved.is_zero_length = array_type->is_zero_length;\n        resolved.is_incomplete =\n            array_type->element_count == 0U && !array_type->is_zero_length;\n        resolved.has_materialized_type = true;\n    } else if (expression->kind == MINIC_EXPRESSION_LOCAL) {\n''',
    "global array semantic query",
)

replace_once(
    "src/core/core_lower.c",
    '''        if (!core_scalar_expression_value_type(context->body, true_expression, &true_type) ||\n            !core_scalar_expression_value_type(context->body, false_expression, &false_type) ||\n            !minic_type_equal(true_type, expression->type) ||\n            !minic_type_equal(false_type, expression->type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n''',
    '''        if (!core_scalar_expression_value_type(context->body, true_expression, &true_type) ||\n            !core_scalar_expression_value_type(context->body, false_expression, &false_type) ||\n            !minic_type_is_integer(true_type) || !minic_type_is_integer(false_type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n''',
    "conditional integer arm types",
)

replace_once(
    "src/core/core_lower.c",
    '''        status = lower_expression(context, expression->value.conditional.when_true, &arm_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = store_scalar_value(\n            context, true_expression->span, expression->type, result_object, arm_value);\n''',
    '''        status = lower_expression(context, expression->value.conditional.when_true, &arm_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = append_integer_conversion(\n            context, true_expression->span, expression->type, arm_value, &arm_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = store_scalar_value(\n            context, true_expression->span, expression->type, result_object, arm_value);\n''',
    "conditional true arm normalization",
)

replace_once(
    "src/core/core_lower.c",
    '''        status = lower_expression(context, expression->value.conditional.when_false, &arm_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = store_scalar_value(\n            context, false_expression->span, expression->type, result_object, arm_value);\n''',
    '''        status = lower_expression(context, expression->value.conditional.when_false, &arm_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = append_integer_conversion(\n            context, false_expression->span, expression->type, arm_value, &arm_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = store_scalar_value(\n            context, false_expression->span, expression->type, result_object, arm_value);\n''',
    "conditional false arm normalization",
)

print("M31B_FIXUPS_APPLIED")
