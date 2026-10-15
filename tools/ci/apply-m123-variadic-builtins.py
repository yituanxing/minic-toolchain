#!/usr/bin/env python3
"""Stage target-neutral Core ownership for GNU va_start/va_copy/va_end."""

from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


def replace_region(path: str, begin: str, end: str, replacement: str) -> None:
    p = Path(path)
    text = p.read_text()
    start = text.find(begin)
    if start < 0:
        raise SystemExit(f"{path}: missing region begin {begin!r}")
    finish = text.find(end, start)
    if finish < 0:
        raise SystemExit(f"{path}: missing region end {end!r}")
    p.write_text(text[:start] + replacement + text[finish:])


# Core IR owns the semantic request for the initial variadic-argument cursor.
# No physical register, stack offset, XLEN, or ABI layout enters Core.
replace_once(
    "src/core/core_ir.h",
    """    MINIC_CORE_INSTRUCTION_COMPILER_BARRIER,\n    MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS,\n    MINIC_CORE_INSTRUCTION_CALL,\n""",
    """    MINIC_CORE_INSTRUCTION_COMPILER_BARRIER,\n    MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS,\n    /* M123_VARIADIC_ARGUMENT_ADDRESS: semantic origin of a va_list cursor.\n       Backend ABI owns register-save-area placement and the concrete address. */\n    MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS,\n    MINIC_CORE_INSTRUCTION_CALL,\n""",
)

replace_once(
    "src/core/core_ir.c",
    """               minic_type_pointee(instruction->type, &pointee) && minic_type_is_void(pointee);\n    }\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n""",
    """               minic_type_pointee(instruction->type, &pointee) && minic_type_is_void(pointee);\n    }\n    /* M123_VARIADIC_ARGUMENT_ADDRESS: Core validates only that the semantic\n       cursor is represented as a pointer value. Whether a target ABI can\n       materialize it is a backend capability question. */\n    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:\n        return instruction_result_is_valid(function, instruction) &&\n               minic_type_is_pointer(instruction->type);\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n""",
)

replace_once(
    "src/core/core_ir.c",
    """                       instruction->value.call_frame_address.level) >= 0;\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n""",
    """                       instruction->value.call_frame_address.level) >= 0;\n    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = variadic.argument.address\\n\",\n                       instruction->result) >= 0;\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n""",
)

# Lower the three current pointer-based va builtins as one semantic family.
# va_start gets a dedicated target-neutral cursor-origin instruction; va_copy
# is ordinary pointer assignment; va_end preserves target-address evaluation.
core_lower = Path("src/core/core_lower.c")
text = core_lower.read_text()
marker = "    /* M122_DISCARDED_COMMA_EFFECT_SEQUENCE:"
if text.count(marker) != 1:
    raise SystemExit(f"src/core/core_lower.c: M122 marker count={text.count(marker)}")
insert = r'''    /* M123_VARIADIC_ARGUMENT_ADDRESS: GNU va builtins are parsed and
       type-checked by frontend/Sema. Core owns their target-neutral execution
       semantics while the selected ABI/backend owns the register save area.
       The current frontend va_list model is a modifiable pointer lvalue. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_START) {
        const MinicExpression *target;
        MinicCoreInstruction instruction;
        MinicCoreLowerStatus status;
        MinicCoreValueId cursor_value;
        MinicCoreValueId target_address;
        MinicType value_type;

        target_id = expression->value.unary.operand;
        target = minic_c0_program_expression(context->body->program, target_id);
        if (context->source_function == NULL || !context->source_function->is_variadic ||
            target == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_pointer(target->type) || minic_type_is_const(target->type) ||
            !minic_type_unqualified(target->type, &value_type) ||
            !minic_type_is_pointer(value_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_address(context, target_id, &target_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &cursor_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = target_address;
        instruction.value.store.stored_value = cursor_value;
        instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_COPY) {
        return lower_assignment_pair(context,
                                     expression->value.binary.left,
                                     expression->value.binary.right,
                                     expression->span,
                                     NULL);
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_END) {
        MinicCoreValueId discarded_address;

        return lower_address(context, expression->value.unary.operand, &discarded_address);
    }

'''
core_lower.write_text(text.replace(marker, insert + marker, 1))

# RV64 Core backend: derive the fixed-prefix ABI cursor, reserve the remaining
# integer argument register save area at the top of the frame (contiguous with
# incoming stack arguments), save aN..a7 in the prologue, and materialize its
# address for the Core semantic instruction.
replace_once(
    "src/target/riscv64/core_codegen.c",
    """typedef struct MinicRiscv64CoreFrame {\n    size_t frame_size;\n    size_t object_count;\n    size_t value_count;\n    size_t value_base_offset;\n    size_t return_address_offset;\n    bool saves_return_address;\n} MinicRiscv64CoreFrame;\n""",
    """typedef struct MinicRiscv64CoreFrame {\n    size_t frame_size;\n    size_t object_count;\n    size_t value_count;\n    size_t value_base_offset;\n    size_t return_address_offset;\n    size_t varargs_offset;\n    size_t varargs_size;\n    size_t integer_parameter_count;\n    bool saves_return_address;\n    bool has_variadic_argument_address;\n} MinicRiscv64CoreFrame;\n""",
)

new_frame_region = r'''static bool core_function_uses_variadic_argument_address(
    const MinicCoreFunction *function) {
    size_t instruction_index;

    if (function == NULL) {
        return false;
    }
    for (instruction_index = 0U; instruction_index < function->instruction_count;
         ++instruction_index) {
        if (function->instructions[instruction_index].kind ==
            MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS) {
            return true;
        }
    }
    return false;
}

static bool core_variadic_fixed_prefix(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       size_t *integer_parameter_count) {
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t parameter_index;

    if (program == NULL || function == NULL || integer_parameter_count == NULL ||
        !minic_riscv64_abi_cursor_initialize_for_return(
            program, function->return_type, &cursor, &return_value)) {
        return false;
    }
    (void)return_value;
    for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
        MinicRiscv64AbiArgumentLocation location;

        if (!minic_riscv64_abi_place_argument(
                program, function->parameter_types[parameter_index], true, &cursor, &location)) {
            return false;
        }
    }
    /* Match the established RV64 frontend/backend contract: va_start is
       currently supported only while all named parameters fit before the
       incoming stack-argument area. */
    if (cursor.stack_slot_count != 0U || cursor.integer_register_count > 8U) {
        return false;
    }
    *integer_parameter_count = cursor.integer_register_count;
    return true;
}

static bool core_frame_initialize(const MinicC0Program *program,
                                  const MinicCoreFunction *function,
                                  MinicRiscv64CoreFrame *frame) {
    size_t object_index;
    size_t storage_size;
    size_t required_size;

    if (function == NULL || frame == NULL) {
        return false;
    }
    storage_size = 0U;
    for (object_index = 0U; object_index < function->object_count; ++object_index) {
        size_t object_size;
        size_t object_alignment;

        if (!minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    function->objects[object_index].type,
                                    &object_size,
                                    &object_alignment) ||
            object_size == 0U || object_alignment == 0U || object_alignment > 16U ||
            function->objects[object_index].element_count == 0U ||
            object_size > SIZE_MAX / function->objects[object_index].element_count) {
            return false;
        }
        object_size *= function->objects[object_index].element_count;
        if (!align_up(storage_size, object_alignment, &storage_size) ||
            storage_size > SIZE_MAX - object_size) {
            return false;
        }
        storage_size += object_size;
    }
    if (!align_up(storage_size, 8U, &frame->value_base_offset) ||
        function->value_count > (SIZE_MAX - frame->value_base_offset) / 8U) {
        return false;
    }
    storage_size = frame->value_base_offset + function->value_count * 8U;
    frame->saves_return_address = core_function_needs_saved_return_address(function);
    frame->return_address_offset = 0U;
    if (frame->saves_return_address) {
        if (!align_up(storage_size, 8U, &frame->return_address_offset) ||
            frame->return_address_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->return_address_offset + 8U;
    }

    frame->has_variadic_argument_address =
        core_function_uses_variadic_argument_address(function);
    frame->integer_parameter_count = 0U;
    frame->varargs_size = 0U;
    if (frame->has_variadic_argument_address) {
        if (!core_variadic_fixed_prefix(
                program, function, &frame->integer_parameter_count)) {
            return false;
        }
        frame->varargs_size = (8U - frame->integer_parameter_count) * 8U;
    }
    if (storage_size > SIZE_MAX - frame->varargs_size) {
        return false;
    }
    required_size = storage_size + frame->varargs_size;
    if (!align_up(required_size, 16U, &frame->frame_size) ||
        frame->frame_size < frame->varargs_size) {
        return false;
    }
    frame->varargs_offset = frame->frame_size - frame->varargs_size;
    if (frame->varargs_offset < storage_size) {
        return false;
    }
    frame->object_count = function->object_count;
    frame->value_count = function->value_count;
    return true;
}

'''
replace_region(
    "src/target/riscv64/core_codegen.c",
    "static bool core_frame_initialize(",
    "static bool core_object_offset(",
    new_frame_region,
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:\n        return core_call_frame_address_supported(instruction);\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n""",
    """    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:\n        return core_call_frame_address_supported(instruction);\n    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {\n        size_t integer_parameter_count;\n\n        return program != NULL && instruction->result < function->value_count &&\n               minic_type_equal(function->values[instruction->result].type, instruction->type) &&\n               minic_type_is_pointer(instruction->type) &&\n               core_variadic_fixed_prefix(program, function, &integer_parameter_count);\n    }\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n""",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return emit_parameter(file, program, function, frame, instruction);\n""",
    """        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:\n        if (!frame->has_variadic_argument_address ||\n            !emit_sp_address(file, \"t0\", frame->varargs_offset)) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return emit_parameter(file, program, function, frame, instruction);\n""",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    if (frame.saves_return_address &&\n        !minic_riscv64_emit_sp_store64(file, \"ra\", frame.return_address_offset)) {\n        return false;\n    }\n    if (fprintf(file, \"  j .L%s_core_bb%\" PRIu32 \"\\n\", symbol_name, function->entry_block) < 0) {\n""",
    """    if (frame.saves_return_address &&\n        !minic_riscv64_emit_sp_store64(file, \"ra\", frame.return_address_offset)) {\n        return false;\n    }\n    if (frame.has_variadic_argument_address) {\n        size_t register_index;\n\n        for (register_index = frame.integer_parameter_count; register_index < 8U;\n             ++register_index) {\n            size_t offset = frame.varargs_offset +\n                            (register_index - frame.integer_parameter_count) * 8U;\n            if (!minic_riscv64_emit_sp_store64(\n                    file, minic_core_rv64_argument_registers[register_index], offset)) {\n                return false;\n            }\n        }\n    }\n    if (fprintf(file, \"  j .L%s_core_bb%\" PRIu32 \"\\n\", symbol_name, function->entry_block) < 0) {\n""",
)

print("staged M123 variadic builtin Core/ABI ownership")
