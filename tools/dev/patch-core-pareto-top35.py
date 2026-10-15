#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}")
    p.write_text(text.replace(old, new, 1))


lower = "src/core/core_lower.c"
codegen = "src/target/riscv64/core_codegen.c"

# M109: aggregate assignment expressions are address-backed Core rvalues.
# The outer record-copy statement must allow a nested assignment producer to
# reach lower_record_value_address instead of rejecting it at the old semantic
# preclassification gate.
replace_once(
    lower,
    """    bool direct_record_call;\n""",
    """    bool direct_record_call;\n    bool record_assignment_value;\n""",
    "record-copy-assignment-decl",
)
replace_once(
    lower,
    """    direct_record_call =\n        source != NULL && source->kind == MINIC_EXPRESSION_CALL &&\n        source->value.call.function_id != MINIC_FUNCTION_INVALID;\n""",
    """    direct_record_call =\n        source != NULL && source->kind == MINIC_EXPRESSION_CALL &&\n        source->value.call.function_id != MINIC_FUNCTION_INVALID;\n    record_assignment_value =\n        source != NULL && source->kind == MINIC_EXPRESSION_ASSIGNMENT;\n""",
    "record-copy-assignment-classify",
)
replace_once(
    lower,
    """        (!direct_record_call &&\n         (!minic_c0_record_value_is_copy_source(context->body->program, statement->expression) ||\n          !minic_c0_record_value_is_address_backed(\n              context->body->program, statement->expression)))) {\n""",
    """        (!direct_record_call && !record_assignment_value &&\n         (!minic_c0_record_value_is_copy_source(context->body->program, statement->expression) ||\n          !minic_c0_record_value_is_address_backed(\n              context->body->program, statement->expression)))) {\n""",
    "record-copy-assignment-gate",
)

replace_once(
    lower,
    """    expression = minic_c0_program_expression(context->body->program, expression_id);\n    if (expression == NULL || !minic_type_is_record(expression->type) ||\n        !minic_c0_record_value_is_address_backed(context->body->program, expression_id)) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    /* M88_RECORD_COMPOUND_LITERAL_ADDRESS: expose the shared semantic backing\n""",
    """    expression = minic_c0_program_expression(context->body->program, expression_id);\n    if (expression == NULL || !minic_type_is_record(expression->type)) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    /* M109_CHAINED_RECORD_ASSIGNMENT_VALUE: an aggregate assignment is an\n       rvalue whose bytes are the fully evaluated RHS. Keep that value\n       address-backed: snapshot the RHS before evaluating the destination, copy\n       the snapshot to the destination, and return the snapshot address. This\n       composes chained assignments without aggregate SSA or target ABI rules. */\n    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT) {\n        const MinicExpression *source;\n        const MinicExpression *target;\n        MinicCoreInstruction operation;\n        MinicCoreObjectId snapshot_object;\n        MinicCoreValueId destination_address;\n        MinicCoreValueId snapshot_address;\n        MinicCoreValueId source_address;\n        MinicCoreLowerStatus status;\n        MinicType expression_type;\n        MinicType pointer_type;\n        MinicType source_type;\n        MinicType target_type;\n\n        target = minic_c0_program_expression(\n            context->body->program, expression->value.binary.left);\n        source = minic_c0_program_expression(\n            context->body->program, expression->value.binary.right);\n        if (target == NULL || source == NULL ||\n            target->value_category != MINIC_VALUE_LVALUE ||\n            !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||\n            minic_type_is_const(target->type) || minic_type_is_volatile(target->type) ||\n            minic_type_is_volatile(source->type) ||\n            !minic_type_unqualified(expression->type, &expression_type) ||\n            !minic_type_unqualified(target->type, &target_type) ||\n            !minic_type_unqualified(source->type, &source_type) ||\n            !minic_type_equal(expression_type, target_type) ||\n            !minic_type_equal(expression_type, source_type) ||\n            !minic_type_is_record(expression_type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        status = lower_record_value_address(\n            context, expression->value.binary.right, &source_address);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (!minic_core_function_add_object(\n                context->function, expression->span, expression_type, &snapshot_object) ||\n            !minic_type_pointer_to(expression_type, &pointer_type)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        (void)memset(&operation, 0, sizeof(operation));\n        operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;\n        operation.span = expression->span;\n        operation.type = pointer_type;\n        operation.result = MINIC_CORE_VALUE_INVALID;\n        operation.value.object_id = snapshot_object;\n        if (!minic_core_function_append_value_instruction(\n                context->function, context->block_id, &operation, &snapshot_address)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        (void)memset(&operation, 0, sizeof(operation));\n        operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;\n        operation.span = expression->span;\n        operation.type = expression_type;\n        operation.result = MINIC_CORE_VALUE_INVALID;\n        operation.value.record_copy.destination_address = snapshot_address;\n        operation.value.record_copy.source_address = source_address;\n        if (!minic_core_function_append_effect_instruction(\n                context->function, context->block_id, &operation)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        status = lower_address(\n            context, expression->value.binary.left, &destination_address);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        (void)memset(&operation, 0, sizeof(operation));\n        operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;\n        operation.span = expression->span;\n        operation.type = pointer_type;\n        operation.result = MINIC_CORE_VALUE_INVALID;\n        operation.value.object_id = snapshot_object;\n        if (!minic_core_function_append_value_instruction(\n                context->function, context->block_id, &operation, &snapshot_address)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        (void)memset(&operation, 0, sizeof(operation));\n        operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;\n        operation.span = expression->span;\n        operation.type = expression_type;\n        operation.result = MINIC_CORE_VALUE_INVALID;\n        operation.value.record_copy.destination_address = destination_address;\n        operation.value.record_copy.source_address = snapshot_address;\n        if (!minic_core_function_append_effect_instruction(\n                context->function, context->block_id, &operation)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        *address_id = snapshot_address;\n        return MINIC_CORE_LOWER_OK;\n    }\n    if (!minic_c0_record_value_is_address_backed(\n            context->body->program, expression_id)) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    /* M88_RECORD_COMPOUND_LITERAL_ADDRESS: expose the shared semantic backing\n""",
    "record-assignment-address-value",
)

# M110/M111: use the existing generic structured-asm IR for two high-frequency
# families. No CSR spelling or Linux identity enters Core: one family is pure
# write-only register outputs, the other is pure scalar register inputs.
anchor = """    /* M68_STRUCTURED_INLINE_ASM_OPTIONAL_INPUTS: M67's structured\n       operand model is variable-sized. Admit the same proven output/memory\n       shape with 0..2 scalar register inputs instead of hard-coding two. */\n"""
insert = r'''    /* M110_PURE_REGISTER_OUTPUT_ASM: ordinary volatile extended asm
       with 1..5 write-only register outputs and no inputs/clobbers. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL &&
        source->output_count >= 1U && source->output_count <= 5U &&
        source->input_count == 0U && source->label_count == 0U &&
        source->register_clobber_count == 0U && !source->has_memory_clobber &&
        source->clobber_count == 0U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t output_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *output_expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY ||
                !core_inline_asm_register_output_constraint(operand) ||
                output_expression == NULL ||
                output_expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(output_expression->type) ||
                !minic_type_unqualified(output_expression->type, &value_type) ||
                !core_memory_scalar_type(value_type) ||
                (output_expression->kind == MINIC_EXPRESSION_LOCAL &&
                 minic_c0_program_local_fixed_register_binding(
                     context->body->program, output_expression->value.local_id) != NULL)) {
                supported_shape = false;
                break;
            }
        }
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added = minic_core_function_add_opaque_inline_asm(context->function,
                                                                    numeric_template,
                                                                    numeric_template_length,
                                                                    true,
                                                                    false,
                                                                    &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = source->output_count;
            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                MinicCoreLowerStatus output_status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                binding->operand_index = output_index;
                output_status = lower_address(
                    context, source->outputs[output_index].expression, &binding->value);
                if (output_status != MINIC_CORE_LOWER_OK) {
                    return output_status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    /* M111_PURE_REGISTER_INPUT_ASM: 1..4 read-only scalar register inputs,
       no outputs/clobbers. This is the input-side dual of M110. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U &&
        source->inputs != NULL && source->input_count >= 1U && source->input_count <= 4U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber && source->clobber_count == 0U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t input_index;
        bool supported_shape = true;

        for (input_index = 0U; input_index < source->input_count; ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *input_expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                !core_inline_asm_constraint_is(operand, "r") ||
                input_expression == NULL ||
                !core_scalar_expression_value_type(context->body, input_expression, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
                break;
            }
        }
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added = minic_core_function_add_opaque_inline_asm(context->function,
                                                                    numeric_template,
                                                                    numeric_template_length,
                                                                    true,
                                                                    false,
                                                                    &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = source->input_count;
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[input_index];
                MinicCoreLowerStatus input_status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = input_index;
                input_status = lower_expression(
                    context, source->inputs[input_index].expression, &binding->value);
                if (input_status != MINIC_CORE_LOWER_OK) {
                    return input_status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

''' + anchor
replace_once(lower, anchor, insert, "pure-register-asm-families")

# RV64 structured-asm tier. Preserve existing mixed-shape register choices,
# while giving output-only and input-only forms enough temporaries.
replace_once(
    codegen,
    """    if (!((register_outputs == 0U && register_readwrites == 1U && memory_outputs == 1U &&\n""",
    """    if (!((register_outputs >= 1U && register_outputs <= 5U &&\n           register_readwrites == 0U && memory_outputs == 0U &&\n           memory_readwrites == 0U && scalar_inputs == 0U &&\n           instruction->value.structured_inline_asm.operand_count == register_outputs &&\n           !inline_asm->has_memory_clobber && inline_asm->register_clobber_count == 0U &&\n           fixed_bindings == 0U) ||\n          (register_outputs == 0U && register_readwrites == 0U &&\n           memory_outputs == 0U && memory_readwrites == 0U &&\n           scalar_inputs >= 1U && scalar_inputs <= 4U &&\n           instruction->value.structured_inline_asm.operand_count == scalar_inputs &&\n           !inline_asm->has_memory_clobber && inline_asm->register_clobber_count == 0U &&\n           fixed_bindings == 0U) ||\n          (register_outputs == 0U && register_readwrites == 1U && memory_outputs == 1U &&\n""",
    "rv64-pure-register-shapes",
)
replace_once(
    codegen,
    """    static const char *const output_registers[2] = {\"t0\", \"t1\"};\n""",
    """    static const char *const output_registers[5] = {\"t0\", \"t1\", \"t2\", \"t3\", \"t4\"};\n""",
    "rv64-output-register-pool",
)
replace_once(
    codegen,
    """    static const char *const input_registers[2] = {\"t3\", \"t4\"};\n""",
    """    static const char *const input_registers[4] = {\"t3\", \"t4\", \"t5\", \"t6\"};\n""",
    "rv64-input-register-pool",
)
replace_once(
    codegen,
    """                while (output_index < 2U &&\n                       core_inline_asm_clobbers_register(\n                           inline_asm, output_registers[output_index])) {\n                    output_index += 1U;\n                }\n                if (output_index >= 2U) {\n""",
    """                while (output_index < 5U &&\n                       core_inline_asm_clobbers_register(\n                           inline_asm, output_registers[output_index])) {\n                    output_index += 1U;\n                }\n                if (output_index >= 5U) {\n""",
    "rv64-output-register-limit",
)
replace_once(
    codegen,
    """                if (input_index >= 2U) {\n                    return false;\n                }\n                register_name = input_registers[input_index++];\n""",
    """                if (input_index >= 4U) {\n                    return false;\n                }\n                register_name = input_registers[input_index++];\n""",
    "rv64-input-register-limit",
)

print("Core Pareto Top24 semantic patch applied")
