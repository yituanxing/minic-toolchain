#!/usr/bin/env python3
from pathlib import Path

core_path = Path("src/core/core_lower.c")
test_path = Path("tests/compiler/c0/pointer_compound_subtraction.c")
runner_path = Path("tests/compiler/c0/run-pointer-compound-subtraction.sh")

source = core_path.read_text()
start_marker = "    /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: pointer += / -= evaluates"
end_marker = "\n    /* M115_CHAINED_BIT_FIELD_ASSIGNMENT:"
begin = source.find(start_marker)
if begin < 0:
    raise SystemExit("M75 pointer compound lowering marker not found")
end = source.find(end_marker, begin)
if end < 0:
    raise SystemExit("M75 pointer compound lowering end marker not found")
region = source[begin:end]
if "M126A_POINTER_COMPOUND_BLOCK_LOCAL" in region:
    raise SystemExit("M126A pointer compound transport already staged")

declarations_old = """        MinicCoreInstruction store;\n        MinicCoreValueId address;\n        MinicCoreValueId current;\n        MinicCoreValueId index;\n        MinicCoreValueId updated;\n        MinicCoreLowerStatus status;\n        MinicType expression_value_type;\n        MinicType index_type;\n        MinicType stored_type;\n"""
declarations_new = """        MinicCoreInstruction store;\n        MinicCoreObjectId address_object;\n        MinicCoreObjectId current_object;\n        MinicCoreValueId address;\n        MinicCoreValueId current;\n        MinicCoreValueId index;\n        MinicCoreValueId updated;\n        MinicCoreLowerStatus status;\n        MinicType address_type;\n        MinicType expression_value_type;\n        MinicType index_type;\n        MinicType stored_type;\n"""
if declarations_old not in region:
    raise SystemExit("M75 declaration shape changed")
region = region.replace(declarations_old, declarations_new, 1)

before_rhs_old = """            if (!minic_core_function_append_value_instruction(\n                    context->function, context->block_id, &instruction, &current)) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            status = lower_expression(context, expression->value.binary.right, &index);\n"""
before_rhs_new = """            if (!minic_core_function_append_value_instruction(\n                    context->function, context->block_id, &instruction, &current)) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            /* M126A_POINTER_COMPOUND_BLOCK_LOCAL: the RHS may create CFG (for\n               example a call with a conditional argument). Preserve both the\n               destination address and its pre-update pointer value before\n               lowering that RHS so POINTER_OFFSET and STORE are formed from\n               values reloaded in the final RHS block. */\n            if (address >= context->function->value_count) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            address_type = context->function->values[address].type;\n            status = spill_scalar_value(\n                context, target->span, address_type, address, &address_object);\n            if (status != MINIC_CORE_LOWER_OK) {\n                return status;\n            }\n            status = spill_scalar_value(\n                context, target->span, stored_type, current, &current_object);\n            if (status != MINIC_CORE_LOWER_OK) {\n                return status;\n            }\n            status = lower_expression(context, expression->value.binary.right, &index);\n"""
if before_rhs_old not in region:
    raise SystemExit("M75 pre-RHS shape changed")
region = region.replace(before_rhs_old, before_rhs_new, 1)

post_rhs_old = """            if (index >= context->function->value_count ||\n                !minic_type_equal(context->function->values[index].type, index_type)) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            (void)memset(&instruction, 0, sizeof(instruction));\n"""
post_rhs_new = """            if (index >= context->function->value_count ||\n                !minic_type_equal(context->function->values[index].type, index_type)) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            status = reload_scalar_value(\n                context, target->span, stored_type, current_object, &current);\n            if (status != MINIC_CORE_LOWER_OK) {\n                return status;\n            }\n            status = reload_scalar_value(\n                context, target->span, address_type, address_object, &address);\n            if (status != MINIC_CORE_LOWER_OK) {\n                return status;\n            }\n            (void)memset(&instruction, 0, sizeof(instruction));\n"""
if post_rhs_old not in region:
    raise SystemExit("M75 post-RHS shape changed")
region = region.replace(post_rhs_old, post_rhs_new, 1)
source = source[:begin] + region + source[end:]
core_path.write_text(source)

test_source = test_path.read_text()
if "read_cfg_adjusted" not in test_source:
    test_source += """\nint pointer_step(int value);\n\nchar *read_cfg_adjusted(char *pointer, int condition) {\n    pointer += pointer_step(condition ? 1 : 2);\n    return pointer;\n}\n"""
    test_path.write_text(test_source)

runner = runner_path.read_text()
needle = """\"$minic\" -S \"$work/pointer_compound_subtraction.i\" \\\n    -o \"$work/pointer_compound_subtraction.s\"\n\ntest -s \"$work/pointer_compound_subtraction.s\"\ngrep -F 'read_adjusted:' \"$work/pointer_compound_subtraction.s\" >/dev/null\nprintf '%s\\n' 'PASS compiler/c0/pointer_compound_subtraction plus-equal=1 minus-equal=1 complete-pointee=1'\n"""
replacement = """\"$minic\" -S \"$work/pointer_compound_subtraction.i\" \\\n    -o \"$work/pointer_compound_subtraction.s\"\nMINIC_CORE_IR=strict \"$minic\" -S \"$work/pointer_compound_subtraction.i\" \\\n    -o \"$work/pointer_compound_subtraction.strict.s\"\n\ntest -s \"$work/pointer_compound_subtraction.s\"\ntest -s \"$work/pointer_compound_subtraction.strict.s\"\ngrep -F 'read_adjusted:' \"$work/pointer_compound_subtraction.s\" >/dev/null\ngrep -F 'read_cfg_adjusted:' \"$work/pointer_compound_subtraction.strict.s\" >/dev/null\nprintf '%s\\n' 'PASS compiler/c0/pointer_compound_subtraction plus-equal=1 minus-equal=1 complete-pointee=1 block-local-rhs=1'\n"""
if "block-local-rhs=1" not in runner:
    if needle not in runner:
        raise SystemExit("pointer compound runner shape changed")
    runner = runner.replace(needle, replacement, 1)
    runner_path.write_text(runner)

print("M126A pointer compound block-local transport staged")
