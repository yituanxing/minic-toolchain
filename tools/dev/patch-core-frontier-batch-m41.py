#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M41 {label}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


old = '''    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:\n        return instruction->result == MINIC_CORE_VALUE_INVALID &&\n               minic_type_is_void(instruction->type) &&\n               instruction->value.parameter_object.parameter_index < function->parameter_count &&\n               instruction->value.parameter_object.object_id < function->object_count &&\n               minic_type_is_record(\n                   function\n                       ->parameter_types[instruction->value.parameter_object.parameter_index]) &&\n               minic_type_equal(\n                   function->parameter_types[instruction->value.parameter_object.parameter_index],\n                   function->objects[instruction->value.parameter_object.object_id].type);\n'''
new = '''    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT: {\n        MinicType object_value_type;\n        MinicType parameter_value_type;\n\n        if (instruction->result != MINIC_CORE_VALUE_INVALID ||\n            !minic_type_is_void(instruction->type) ||\n            instruction->value.parameter_object.parameter_index >= function->parameter_count ||\n            instruction->value.parameter_object.object_id >= function->object_count) {\n            return false;\n        }\n        /* Top-level cv-qualification belongs to the callee's local parameter\n         * object.  The incoming ABI value/signature is the unqualified value\n         * type, so parameter ingress must compare those value types rather\n         * than requiring the local storage type to be byte-for-byte equal. */\n        if (!minic_type_unqualified(\n                function->parameter_types[instruction->value.parameter_object.parameter_index],\n                &parameter_value_type) ||\n            !minic_type_unqualified(\n                function->objects[instruction->value.parameter_object.object_id].type,\n                &object_value_type)) {\n            return false;\n        }\n        return minic_type_is_record(parameter_value_type) &&\n               minic_type_equal(parameter_value_type, object_value_type);\n    }\n'''
replace_once("src/core/core_ir.c", old, new, "qualified aggregate parameter verifier")
print("M41_PATCH_APPLIED")
