#!/usr/bin/env python3
"""Add Core IR lowering/emission for __builtin_return_address(0)."""

from pathlib import Path

MARKER = "M79_CALL_FRAME_RETURN_ADDRESS"
IR = Path("src/core/core_ir.h")
IR_IMPL = Path("src/core/core_ir.c")
LOWER = Path("src/core/core_lower.c")
CODEGEN = Path("src/target/riscv64/core_codegen.c")


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M79 {name} anchor count={count}")
    return text.replace(old, new, 1)


def patch_ir() -> None:
    text = IR.read_text()
    if MARKER in text:
        print("M79 core_ir.h already applied")
        return

    enum_anchor = '''typedef enum MinicCoreInstructionKind {\n'''
    enum_insert = '''/* M79_CALL_FRAME_RETURN_ADDRESS: target-neutral semantic origin for\n   GNU call-frame address builtins. Backends may support only a subset of\n   kind/level pairs; unsupported pairs remain fail-closed. */\ntypedef enum MinicCoreCallFrameAddressKind {\n    MINIC_CORE_CALL_FRAME_ADDRESS_RETURN = 0,\n    MINIC_CORE_CALL_FRAME_ADDRESS_FRAME\n} MinicCoreCallFrameAddressKind;\n\ntypedef enum MinicCoreInstructionKind {\n'''
    text = replace_once(text, enum_anchor, enum_insert, "ir-enum")

    kind_anchor = '''    MINIC_CORE_INSTRUCTION_COMPILER_BARRIER,\n    MINIC_CORE_INSTRUCTION_CALL\n'''
    kind_replacement = '''    MINIC_CORE_INSTRUCTION_COMPILER_BARRIER,\n    MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS,\n    MINIC_CORE_INSTRUCTION_CALL\n'''
    text = replace_once(text, kind_anchor, kind_replacement, "ir-kind")

    union_anchor = '''        size_t fixed_register_binding_id;\n        struct {\n            size_t parameter_index;\n'''
    union_replacement = '''        size_t fixed_register_binding_id;\n        struct {\n            MinicCoreCallFrameAddressKind kind;\n            unsigned int level;\n        } call_frame_address;\n        struct {\n            size_t parameter_index;\n'''
    text = replace_once(text, union_anchor, union_replacement, "ir-union")
    IR.write_text(text)
    print("M79 core_ir.h applied")


def patch_ir_impl() -> None:
    text = IR_IMPL.read_text()
    if MARKER in text:
        print("M79 core_ir.c already applied")
        return

    valid_anchor = '''    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return instruction_result_is_valid(function, instruction) &&\n               instruction->value.parameter_index < function->parameter_count &&\n'''
    valid_replacement = '''    /* M79_CALL_FRAME_RETURN_ADDRESS: Core validates the semantic shape only.\n       Backend support for a particular kind/level pair is a target concern. */\n    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS: {\n        MinicType pointee;\n\n        return instruction_result_is_valid(function, instruction) &&\n               (instruction->value.call_frame_address.kind ==\n                    MINIC_CORE_CALL_FRAME_ADDRESS_RETURN ||\n                instruction->value.call_frame_address.kind ==\n                    MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) &&\n               minic_type_pointee(instruction->type, &pointee) && minic_type_is_void(pointee);\n    }\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return instruction_result_is_valid(function, instruction) &&\n               instruction->value.parameter_index < function->parameter_count &&\n'''
    text = replace_once(text, valid_anchor, valid_replacement, "ir-valid")

    dump_anchor = '''    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return fprintf(output,\n                       "  %%%" PRIu32 " = parameter %zu\\n",\n                       instruction->result,\n                       instruction->value.parameter_index) >= 0;\n'''
    dump_replacement = '''    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:\n        return fprintf(output,\n                       "  %%%" PRIu32 " = call.frame.%s level=%u\\n",\n                       instruction->result,\n                       instruction->value.call_frame_address.kind ==\n                               MINIC_CORE_CALL_FRAME_ADDRESS_RETURN\n                           ? "return"\n                           : "frame",\n                       instruction->value.call_frame_address.level) >= 0;\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return fprintf(output,\n                       "  %%%" PRIu32 " = parameter %zu\\n",\n                       instruction->result,\n                       instruction->value.parameter_index) >= 0;\n'''
    text = replace_once(text, dump_anchor, dump_replacement, "ir-dump")
    IR_IMPL.write_text(text)
    print("M79 core_ir.c applied")


def patch_lower() -> None:
    text = LOWER.read_text()
    if MARKER in text:
        print("M79 core_lower.c already applied")
        return

    anchor = '''    if (expression->kind == MINIC_EXPRESSION_BITCAST) {\n'''
    replacement = '''    /* M79_CALL_FRAME_RETURN_ADDRESS: keep the semantic builtin in Core rather\n       than lowering it to a target register in the frontend. The first seam\n       is GNU __builtin_return_address(0); deeper levels and frame-address\n       queries remain unsupported until a backend can define them correctly. */\n    if (expression->kind == MINIC_EXPRESSION_CALL_FRAME_ADDRESS) {\n        MinicType pointee;\n\n        if (expression->value.call_frame_address.kind != MINIC_CALL_FRAME_ADDRESS_RETURN ||\n            expression->value.call_frame_address.level != 0U ||\n            !minic_type_pointee(expression->type, &pointee) || !minic_type_is_void(pointee)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind = MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS;\n        instruction.span = expression->span;\n        instruction.type = expression->type;\n        instruction.result = MINIC_CORE_VALUE_INVALID;\n        instruction.value.call_frame_address.kind = MINIC_CORE_CALL_FRAME_ADDRESS_RETURN;\n        instruction.value.call_frame_address.level = 0U;\n        return minic_core_function_append_value_instruction(\n                   context->function, context->block_id, &instruction, value_id)\n                   ? MINIC_CORE_LOWER_OK\n                   : MINIC_CORE_LOWER_ERROR;\n    }\n    if (expression->kind == MINIC_EXPRESSION_BITCAST) {\n'''
    text = replace_once(text, anchor, replacement, "lower-expression")
    LOWER.write_text(text)
    print("M79 core_lower.c applied")


def patch_codegen() -> None:
    text = CODEGEN.read_text()
    if MARKER in text:
        print("M79 core_codegen.c already applied")
        return

    has_call_anchor = '''static bool core_function_has_call(const MinicCoreFunction *function) {\n    size_t instruction_index;\n\n    if (function == NULL) {\n        return false;\n    }\n    for (instruction_index = 0U; instruction_index < function->instruction_count;\n         ++instruction_index) {\n        if (function->instructions[instruction_index].kind == MINIC_CORE_INSTRUCTION_CALL) {\n            return true;\n        }\n    }\n    return false;\n}\n'''
    has_call_replacement = '''/* M79_CALL_FRAME_RETURN_ADDRESS: a return-address query needs the entry\n   value of ra even in a function that has no ordinary Core CALL yet. Save it\n   in the prologue whenever either a call or this semantic instruction exists. */\nstatic bool core_function_needs_saved_return_address(const MinicCoreFunction *function) {\n    size_t instruction_index;\n\n    if (function == NULL) {\n        return false;\n    }\n    for (instruction_index = 0U; instruction_index < function->instruction_count;\n         ++instruction_index) {\n        MinicCoreInstructionKind kind = function->instructions[instruction_index].kind;\n        if (kind == MINIC_CORE_INSTRUCTION_CALL ||\n            kind == MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS) {\n            return true;\n        }\n    }\n    return false;\n}\n'''
    text = replace_once(text, has_call_anchor, has_call_replacement, "saved-ra-scan")
    text = replace_once(text,
                        '''    frame->saves_return_address = core_function_has_call(function);\n''',
                        '''    frame->saves_return_address =\n        core_function_needs_saved_return_address(function);\n''',
                        "saved-ra-use")

    helper_anchor = '''static bool core_scalar_bitcast_supported(const MinicC0Program *program,\n'''
    helper = '''static bool core_call_frame_address_supported(\n    const MinicCoreInstruction *instruction) {\n    MinicType pointee;\n\n    return instruction != NULL &&\n           instruction->kind == MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS &&\n           instruction->value.call_frame_address.kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN &&\n           instruction->value.call_frame_address.level == 0U &&\n           minic_type_pointee(instruction->type, &pointee) && minic_type_is_void(pointee);\n}\n\nstatic bool core_scalar_bitcast_supported(const MinicC0Program *program,\n'''
    text = replace_once(text, helper_anchor, helper, "support-helper")

    support_anchor = '''    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n'''
    support_replacement = '''    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n        return true;\n    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:\n        return core_call_frame_address_supported(instruction);\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n'''
    text = replace_once(text, support_anchor, support_replacement, "support-switch")

    emit_anchor = '''    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return emit_parameter(file, program, function, frame, instruction);\n'''
    emit_replacement = '''    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:\n        if (!core_call_frame_address_supported(instruction) || !frame->saves_return_address ||\n            !minic_riscv64_emit_sp_load64(file, "t0", frame->return_address_offset)) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, "t0");\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return emit_parameter(file, program, function, frame, instruction);\n'''
    text = replace_once(text, emit_anchor, emit_replacement, "emit-switch")

    CODEGEN.write_text(text)
    print("M79 core_codegen.c applied")


def main() -> int:
    patch_ir()
    patch_ir_impl()
    patch_lower()
    patch_codegen()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
