#!/usr/bin/env python3
"""Stage M49: let Core address global arrays and lower array-object subscripts."""

from __future__ import annotations

from pathlib import Path

FILES = {
    "ir": Path("src/core/core_ir.c"),
    "lower": Path("src/core/core_lower.c"),
    "codegen": Path("src/target/riscv64/core_codegen.c"),
}
MARKER = "core_global_addressable_type"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    texts = {name: path.read_text() for name, path in FILES.items()}
    marker_state = {name: MARKER in text for name, text in texts.items()}
    if all(marker_state.values()):
        print("M49 global-array address lowering already applied")
        return 0
    if any(marker_state.values()):
        raise SystemExit(f"partial M49 state: {marker_state}")

    texts["ir"] = replace_once(
        texts["ir"],
        """bool minic_core_function_add_global(MinicCoreFunction *function,\n                                    const char *name,\n""",
        """static bool core_global_addressable_type(MinicType type) {\n    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||\n           minic_type_is_array(type);\n}\n\nbool minic_core_function_add_global(MinicCoreFunction *function,\n                                    const char *name,\n""",
        "core_ir.c addressable helper",
    )
    texts["ir"] = replace_once(
        texts["ir"],
        """        function->global_count >= (size_t)UINT32_MAX ||\n        (!minic_type_is_integer(type) && !minic_type_is_pointer(type))) {\n""",
        """        function->global_count >= (size_t)UINT32_MAX ||\n        !core_global_addressable_type(type)) {\n""",
        "core_ir.c add global type",
    )
    texts["ir"] = replace_once(
        texts["ir"],
        """        if (global->name == NULL || global->name_length == 0U ||\n            (!minic_type_is_integer(global->type) && !minic_type_is_pointer(global->type))) {\n""",
        """        if (global->name == NULL || global->name_length == 0U ||\n            !core_global_addressable_type(global->type)) {\n""",
        "core_ir.c verify global type",
    )

    texts["lower"] = replace_once(
        texts["lower"],
        """static bool core_memory_scalar_type(MinicType type) {\n    return minic_type_is_integer(type) || minic_type_is_pointer(type);\n}\n""",
        """static bool core_memory_scalar_type(MinicType type) {\n    return minic_type_is_integer(type) || minic_type_is_pointer(type);\n}\n\nstatic bool core_global_addressable_type(MinicType type) {\n    return core_memory_scalar_type(type) || minic_type_is_array(type);\n}\n""",
        "core_lower.c addressable helper",
    )
    texts["lower"] = replace_once(
        texts["lower"],
        """        if (!core_memory_scalar_type(global->type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n""",
        """        if (!core_global_addressable_type(global->type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n""",
        "core_lower.c global address type",
    )
    old_subscript = """    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {\n        const MinicExpression *base;\n        const MinicExpression *index;\n        MinicCoreInstruction offset_instruction;\n        MinicCoreObjectId base_object;\n        MinicCoreValueId base_value;\n        MinicCoreValueId index_value;\n        MinicCoreLowerStatus subscript_status;\n        MinicType element_type;\n        size_t element_size;\n\n        base =\n            minic_c0_program_expression(context->body->program, expression->value.subscript.base);\n        index =\n            minic_c0_program_expression(context->body->program, expression->value.subscript.index);\n        if (base == NULL || index == NULL || !minic_type_is_pointer(base->type) ||\n            !minic_type_is_integer(index->type) || !minic_type_pointee(base->type, &element_type) ||\n            !minic_type_equal(element_type, expression->type) ||\n            !minic_c0_pointer_arithmetic_element_size(\n                context->body->program, minic_default_data_layout(), base->type, &element_size)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        subscript_status = lower_expression(context, expression->value.subscript.base, &base_value);\n        if (subscript_status != MINIC_CORE_LOWER_OK) {\n            return subscript_status;\n        }\n        if (base_value >= context->function->value_count ||\n            !minic_type_equal(context->function->values[base_value].type, base->type)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        subscript_status =\n            spill_scalar_value(context, base->span, base->type, base_value, &base_object);\n        if (subscript_status != MINIC_CORE_LOWER_OK) {\n            return subscript_status;\n        }\n        subscript_status =\n            lower_expression(context, expression->value.subscript.index, &index_value);\n        if (subscript_status != MINIC_CORE_LOWER_OK) {\n            return subscript_status;\n        }\n        if (index_value >= context->function->value_count ||\n            !minic_type_equal(context->function->values[index_value].type, index->type)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        subscript_status =\n            reload_scalar_value(context, base->span, base->type, base_object, &base_value);\n        if (subscript_status != MINIC_CORE_LOWER_OK) {\n            return subscript_status;\n        }\n\n        (void)memset(&offset_instruction, 0, sizeof(offset_instruction));\n        offset_instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;\n        offset_instruction.span = expression->span;\n        offset_instruction.type = base->type;\n        offset_instruction.result = MINIC_CORE_VALUE_INVALID;\n        offset_instruction.value.pointer_offset.base = base_value;\n        offset_instruction.value.pointer_offset.index = index_value;\n        offset_instruction.value.pointer_offset.element_size = element_size;\n        return minic_core_function_append_value_instruction(\n                   context->function, context->block_id, &offset_instruction, address_id)\n                   ? MINIC_CORE_LOWER_OK\n                   : MINIC_CORE_LOWER_ERROR;\n    }\n"""
    new_subscript = """    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {\n        const MinicExpression *base;\n        const MinicExpression *index;\n        MinicArrayObjectInfo array_info;\n        MinicCoreInstruction offset_instruction;\n        MinicCoreObjectId base_object;\n        MinicCoreValueId base_value;\n        MinicCoreValueId index_value;\n        MinicCoreLowerStatus subscript_status;\n        MinicType array_pointer_type;\n        MinicType element_type;\n        MinicType pointer_type;\n        size_t element_size;\n        bool array_base;\n\n        base =\n            minic_c0_program_expression(context->body->program, expression->value.subscript.base);\n        index =\n            minic_c0_program_expression(context->body->program, expression->value.subscript.index);\n        if (base == NULL || index == NULL || !minic_type_is_integer(index->type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        (void)memset(&array_info, 0, sizeof(array_info));\n        array_base = minic_c0_expression_array_object_info(\n            context->body->program, base, &array_info);\n        if (array_base) {\n            if (!array_info.has_materialized_type || !minic_type_is_array(base->type) ||\n                !minic_type_equal(array_info.element_type, expression->type) ||\n                !minic_type_pointer_to(array_info.element_type, &pointer_type) ||\n                !minic_c0_pointer_arithmetic_element_size(context->body->program,\n                                                          minic_default_data_layout(),\n                                                          pointer_type,\n                                                          &element_size)) {\n                return MINIC_CORE_LOWER_UNSUPPORTED;\n            }\n            subscript_status =\n                lower_address(context, expression->value.subscript.base, &base_value);\n            if (subscript_status != MINIC_CORE_LOWER_OK) {\n                return subscript_status;\n            }\n            if (base_value >= context->function->value_count ||\n                !minic_type_pointer_to(base->type, &array_pointer_type) ||\n                !minic_type_equal(context->function->values[base_value].type,\n                                  array_pointer_type)) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            subscript_status = append_scalar_bitcast(\n                context, base->span, pointer_type, base_value, &base_value);\n            if (subscript_status != MINIC_CORE_LOWER_OK) {\n                return subscript_status;\n            }\n        } else {\n            if (!minic_type_is_pointer(base->type) ||\n                !minic_type_pointee(base->type, &element_type) ||\n                !minic_type_equal(element_type, expression->type) ||\n                !minic_c0_pointer_arithmetic_element_size(context->body->program,\n                                                          minic_default_data_layout(),\n                                                          base->type,\n                                                          &element_size)) {\n                return MINIC_CORE_LOWER_UNSUPPORTED;\n            }\n            pointer_type = base->type;\n            subscript_status =\n                lower_expression(context, expression->value.subscript.base, &base_value);\n            if (subscript_status != MINIC_CORE_LOWER_OK) {\n                return subscript_status;\n            }\n            if (base_value >= context->function->value_count ||\n                !minic_type_equal(context->function->values[base_value].type, base->type)) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n        }\n        subscript_status =\n            spill_scalar_value(context, base->span, pointer_type, base_value, &base_object);\n        if (subscript_status != MINIC_CORE_LOWER_OK) {\n            return subscript_status;\n        }\n        subscript_status =\n            lower_expression(context, expression->value.subscript.index, &index_value);\n        if (subscript_status != MINIC_CORE_LOWER_OK) {\n            return subscript_status;\n        }\n        if (index_value >= context->function->value_count ||\n            !minic_type_equal(context->function->values[index_value].type, index->type)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        subscript_status =\n            reload_scalar_value(context, base->span, pointer_type, base_object, &base_value);\n        if (subscript_status != MINIC_CORE_LOWER_OK) {\n            return subscript_status;\n        }\n\n        (void)memset(&offset_instruction, 0, sizeof(offset_instruction));\n        offset_instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;\n        offset_instruction.span = expression->span;\n        offset_instruction.type = pointer_type;\n        offset_instruction.result = MINIC_CORE_VALUE_INVALID;\n        offset_instruction.value.pointer_offset.base = base_value;\n        offset_instruction.value.pointer_offset.index = index_value;\n        offset_instruction.value.pointer_offset.element_size = element_size;\n        return minic_core_function_append_value_instruction(\n                   context->function, context->block_id, &offset_instruction, address_id)\n                   ? MINIC_CORE_LOWER_OK\n                   : MINIC_CORE_LOWER_ERROR;\n    }\n"""
    texts["lower"] = replace_once(texts["lower"], old_subscript, new_subscript, "core_lower.c subscript")

    texts["codegen"] = replace_once(
        texts["codegen"],
        """static bool core_scalar_type(MinicType type) {\n    return minic_type_is_integer(type) || minic_type_is_pointer(type);\n}\n""",
        """static bool core_scalar_type(MinicType type) {\n    return minic_type_is_integer(type) || minic_type_is_pointer(type);\n}\n\nstatic bool core_global_addressable_type(MinicType type) {\n    return core_scalar_type(type) || minic_type_is_array(type);\n}\n""",
        "core_codegen.c addressable helper",
    )
    texts["codegen"] = replace_once(
        texts["codegen"],
        """        if (function->globals[index].name == NULL || function->globals[index].name_length == 0U ||\n            !core_scalar_type(function->globals[index].type)) {\n""",
        """        if (function->globals[index].name == NULL || function->globals[index].name_length == 0U ||\n            !core_global_addressable_type(function->globals[index].type)) {\n""",
        "core_codegen.c global type",
    )

    for name, path in FILES.items():
        path.write_text(texts[name])
    print("M49 global-array address lowering applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
