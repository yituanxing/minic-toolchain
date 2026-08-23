#!/usr/bin/env python3
"""Add Core lowering for pointer += / -= compound assignment."""

from pathlib import Path

MARKER = "M75_POINTER_COMPOUND_ASSIGNMENT_VALUE"


def replace_once(path: Path, anchor: str, replacement: str) -> None:
    text = path.read_text()
    if MARKER in text:
        print(f"M75 already applied: {path}")
        return
    count = text.count(anchor)
    if count != 1:
        raise SystemExit(f"M75 anchor count={count} in {path}")
    path.write_text(text.replace(anchor, replacement, 1))
    print(f"M75 pointer compound assignment applied: {path}")


def main() -> int:
    replace_once(
        Path("src/core/core_ir.h"),
        '''        struct {\n            MinicCoreValueId base;\n            MinicCoreValueId index;\n            size_t element_size;\n        } pointer_offset;\n''',
        '''        struct {\n            MinicCoreValueId base;\n            MinicCoreValueId index;\n            size_t element_size;\n            /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: preserve pointer -=\n               as subtraction instead of negating a potentially unsigned index. */\n            bool subtract;\n        } pointer_offset;\n''',
    )

    lower_path = Path("src/core/core_lower.c")
    text = lower_path.read_text()
    if MARKER not in text:
        anchor = '''    /* M65_SCALAR_ASSIGNMENT_EXPRESSION_VALUE: simple assignment is an\n       expression as well as a side effect. Preserve the assigned scalar across\n'''
        replacement = '''    /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: pointer += / -= evaluates\n       the destination lvalue once, loads its current pointer value, applies a\n       scaled integer offset, stores the updated pointer, and yields that value.\n       Keep subtraction explicit in Core: negating an unsigned RHS before\n       pointer.offset would change its width/extension semantics. */\n    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {\n        const MinicExpression *source;\n        const MinicExpression *target;\n        MinicCoreInstruction store;\n        MinicCoreValueId address;\n        MinicCoreValueId current;\n        MinicCoreValueId index;\n        MinicCoreValueId updated;\n        MinicCoreLowerStatus status;\n        MinicType expression_value_type;\n        MinicType index_type;\n        MinicType stored_type;\n        size_t element_size;\n        bool subtract;\n\n        target = minic_c0_program_expression(\n            context->body->program, expression->value.binary.left);\n        source = minic_c0_program_expression(\n            context->body->program, expression->value.binary.right);\n        subtract = expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT;\n        if (target == NULL || source == NULL ||\n            target->value_category != MINIC_VALUE_LVALUE ||\n            minic_type_is_const(target->type) ||\n            (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&\n             expression->value.binary.operator_kind != MINIC_BINARY_SUBTRACT) ||\n            !minic_type_unqualified(target->type, &stored_type) ||\n            !minic_type_is_pointer(stored_type) ||\n            !minic_type_unqualified(expression->type, &expression_value_type) ||\n            !minic_type_equal(expression_value_type, stored_type) ||\n            !core_scalar_expression_value_type(context->body, source, &index_type) ||\n            !minic_type_is_integer(index_type) ||\n            !minic_c0_pointer_arithmetic_element_size(context->body->program,\n                                                      minic_default_data_layout(),\n                                                      stored_type,\n                                                      &element_size)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        status = lower_address(context, expression->value.binary.left, &address);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;\n        instruction.span = expression->span;\n        instruction.type = stored_type;\n        instruction.result = MINIC_CORE_VALUE_INVALID;\n        instruction.value.load.address = address;\n        instruction.value.load.is_volatile = minic_type_is_volatile(target->type);\n        if (!minic_core_function_append_value_instruction(\n                context->function, context->block_id, &instruction, &current)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        status = lower_expression(context, expression->value.binary.right, &index);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (index >= context->function->value_count ||\n            !minic_type_equal(context->function->values[index].type, index_type)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;\n        instruction.span = expression->span;\n        instruction.type = stored_type;\n        instruction.result = MINIC_CORE_VALUE_INVALID;\n        instruction.value.pointer_offset.base = current;\n        instruction.value.pointer_offset.index = index;\n        instruction.value.pointer_offset.element_size = element_size;\n        instruction.value.pointer_offset.subtract = subtract;\n        if (!minic_core_function_append_value_instruction(\n                context->function, context->block_id, &instruction, &updated)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        (void)memset(&store, 0, sizeof(store));\n        store.kind = MINIC_CORE_INSTRUCTION_STORE;\n        store.span = expression->span;\n        store.type = minic_type_void();\n        store.result = MINIC_CORE_VALUE_INVALID;\n        store.value.store.address = address;\n        store.value.store.stored_value = updated;\n        store.value.store.is_volatile = minic_type_is_volatile(target->type);\n        if (!minic_core_function_append_effect_instruction(\n                context->function, context->block_id, &store)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        *value_id = updated;\n        return MINIC_CORE_LOWER_OK;\n    }\n\n    /* M65_SCALAR_ASSIGNMENT_EXPRESSION_VALUE: simple assignment is an\n       expression as well as a side effect. Preserve the assigned scalar across\n'''
        count = text.count(anchor)
        if count != 1:
            raise SystemExit(f"M75 lower anchor count={count}")
        lower_path.write_text(text.replace(anchor, replacement, 1))
        print(f"M75 pointer compound assignment applied: {lower_path}")
    else:
        print(f"M75 already applied: {lower_path}")

    codegen_path = Path("src/target/riscv64/core_codegen.c")
    text = codegen_path.read_text()
    if MARKER not in text:
        anchor = '''    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:\n        if (!load_core_value(file, frame, instruction->value.pointer_offset.base, "t0") ||\n            !load_core_value(file, frame, instruction->value.pointer_offset.index, "t1")) {\n            return false;\n        }\n        if (instruction->value.pointer_offset.element_size != 1U &&\n            fprintf(file,\n                    "  li t2, %zu\\n"\n                    "  mul t1, t1, t2\\n",\n                    instruction->value.pointer_offset.element_size) < 0) {\n            return false;\n        }\n        if (fprintf(file, "  add t0, t0, t1\\n") < 0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, "t0");\n'''
        replacement = '''    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:\n        if (!load_core_value(file, frame, instruction->value.pointer_offset.base, "t0") ||\n            !load_core_value(file, frame, instruction->value.pointer_offset.index, "t1")) {\n            return false;\n        }\n        if (instruction->value.pointer_offset.element_size != 1U &&\n            fprintf(file,\n                    "  li t2, %zu\\n"\n                    "  mul t1, t1, t2\\n",\n                    instruction->value.pointer_offset.element_size) < 0) {\n            return false;\n        }\n        /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: retain subtraction through\n           the Core boundary so an unsigned index is scaled before `sub`. */\n        if (fprintf(file,\n                    "  %s t0, t0, t1\\n",\n                    instruction->value.pointer_offset.subtract ? "sub" : "add") < 0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, "t0");\n'''
        count = text.count(anchor)
        if count != 1:
            raise SystemExit(f"M75 codegen anchor count={count}")
        codegen_path.write_text(text.replace(anchor, replacement, 1))
        print(f"M75 pointer compound assignment applied: {codegen_path}")
    else:
        print(f"M75 already applied: {codegen_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
