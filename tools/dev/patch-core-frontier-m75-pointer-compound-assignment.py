#!/usr/bin/env python3
"""Add Core lowering for pointer += / -= without shadowing integer compound assignment."""

from pathlib import Path

MARKER = "M75_POINTER_COMPOUND_ASSIGNMENT_VALUE"
DISPATCH_MARKER = "M76_POINTER_COMPOUND_DISPATCH"

POINTER_BLOCK = '''    /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: pointer += / -= evaluates
       the destination lvalue once, loads its current pointer value, applies a
       scaled integer offset, stores the updated pointer, and yields that value.
       Keep subtraction explicit in Core: negating an unsigned RHS before
       pointer.offset would change its width/extension semantics. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicCoreInstruction store;
        MinicCoreValueId address;
        MinicCoreValueId current;
        MinicCoreValueId index;
        MinicCoreValueId updated;
        MinicCoreLowerStatus status;
        MinicType expression_value_type;
        MinicType index_type;
        MinicType stored_type;
        size_t element_size;
        bool subtract;

        target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        if (target == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* M76_POINTER_COMPOUND_DISPATCH: only claim compound assignments whose
           destination is actually a pointer. Integer +=/-=/&=/|=/... must
           continue to the established M51 integer compound-assignment path. */
        if (minic_type_unqualified(target->type, &stored_type) &&
            minic_type_is_pointer(stored_type)) {
            source = minic_c0_program_expression(
                context->body->program, expression->value.binary.right);
            subtract = expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT;
            if (source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(target->type) ||
                (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
                 expression->value.binary.operator_kind != MINIC_BINARY_SUBTRACT) ||
                !minic_type_unqualified(expression->type, &expression_value_type) ||
                !minic_type_equal(expression_value_type, stored_type) ||
                !core_scalar_expression_value_type(context->body, source, &index_type) ||
                !minic_type_is_integer(index_type) ||
                !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                          minic_default_data_layout(),
                                                          stored_type,
                                                          &element_size)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_address(context, expression->value.binary.left, &address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
            instruction.span = expression->span;
            instruction.type = stored_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.load.address = address;
            instruction.value.load.is_volatile = minic_type_is_volatile(target->type);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &current)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = lower_expression(context, expression->value.binary.right, &index);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (index >= context->function->value_count ||
                !minic_type_equal(context->function->values[index].type, index_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
            instruction.span = expression->span;
            instruction.type = stored_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.pointer_offset.base = current;
            instruction.value.pointer_offset.index = index;
            instruction.value.pointer_offset.element_size = element_size;
            instruction.value.pointer_offset.subtract = subtract;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &updated)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&store, 0, sizeof(store));
            store.kind = MINIC_CORE_INSTRUCTION_STORE;
            store.span = expression->span;
            store.type = minic_type_void();
            store.result = MINIC_CORE_VALUE_INVALID;
            store.value.store.address = address;
            store.value.store.stored_value = updated;
            store.value.store.is_volatile = minic_type_is_volatile(target->type);
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &store)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            *value_id = updated;
            return MINIC_CORE_LOWER_OK;
        }
    }

'''


def patch_ir_header() -> None:
    path = Path("src/core/core_ir.h")
    text = path.read_text()
    if MARKER in text:
        print(f"M75 already applied: {path}")
        return
    anchor = '''        struct {\n            MinicCoreValueId base;\n            MinicCoreValueId index;\n            size_t element_size;\n        } pointer_offset;\n'''
    replacement = '''        struct {\n            MinicCoreValueId base;\n            MinicCoreValueId index;\n            size_t element_size;\n            /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: preserve pointer -=\n               as subtraction instead of negating a potentially unsigned index. */\n            bool subtract;\n        } pointer_offset;\n'''
    if text.count(anchor) != 1:
        raise SystemExit("M75 core_ir.h anchor mismatch")
    path.write_text(text.replace(anchor, replacement, 1))


def patch_lower() -> None:
    path = Path("src/core/core_lower.c")
    text = path.read_text()
    m65 = '''    /* M65_SCALAR_ASSIGNMENT_EXPRESSION_VALUE: simple assignment is an\n'''
    if DISPATCH_MARKER in text:
        print(f"M75/M76 already applied: {path}")
        return
    if MARKER in text:
        start = text.index("    /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE:")
        end = text.index(m65, start)
        path.write_text(text[:start] + POINTER_BLOCK + text[end:])
        print(f"M76 pointer dispatch repaired: {path}")
        return
    if text.count(m65) != 1:
        raise SystemExit("M75 lower anchor mismatch")
    path.write_text(text.replace(m65, POINTER_BLOCK + m65, 1))
    print(f"M75/M76 pointer compound assignment applied: {path}")


def patch_codegen() -> None:
    path = Path("src/target/riscv64/core_codegen.c")
    text = path.read_text()
    if MARKER in text:
        print(f"M75 already applied: {path}")
        return
    anchor = '''    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:\n        if (!load_core_value(file, frame, instruction->value.pointer_offset.base, "t0") ||\n            !load_core_value(file, frame, instruction->value.pointer_offset.index, "t1")) {\n            return false;\n        }\n        if (instruction->value.pointer_offset.element_size != 1U &&\n            fprintf(file,\n                    "  li t2, %zu\\n"\n                    "  mul t1, t1, t2\\n",\n                    instruction->value.pointer_offset.element_size) < 0) {\n            return false;\n        }\n        if (fprintf(file, "  add t0, t0, t1\\n") < 0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, "t0");\n'''
    replacement = '''    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:\n        if (!load_core_value(file, frame, instruction->value.pointer_offset.base, "t0") ||\n            !load_core_value(file, frame, instruction->value.pointer_offset.index, "t1")) {\n            return false;\n        }\n        if (instruction->value.pointer_offset.element_size != 1U &&\n            fprintf(file,\n                    "  li t2, %zu\\n"\n                    "  mul t1, t1, t2\\n",\n                    instruction->value.pointer_offset.element_size) < 0) {\n            return false;\n        }\n        /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: retain subtraction through\n           the Core boundary so an unsigned index is scaled before `sub`. */\n        if (fprintf(file,\n                    "  %s t0, t0, t1\\n",\n                    instruction->value.pointer_offset.subtract ? "sub" : "add") < 0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, "t0");\n'''
    if text.count(anchor) != 1:
        raise SystemExit("M75 codegen anchor mismatch")
    path.write_text(text.replace(anchor, replacement, 1))


def main() -> int:
    patch_ir_header()
    patch_lower()
    patch_codegen()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
