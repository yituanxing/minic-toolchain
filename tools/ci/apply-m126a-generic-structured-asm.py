#!/usr/bin/env python3
from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    source = path.read_text()
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, got {count}")
    path.write_text(source.replace(old, new, 1))


def replace_function(path_text: str, start: str, end_marker: str, replacement: str) -> None:
    path = Path(path_text)
    source = path.read_text()
    begin = source.find(start)
    if begin < 0:
        raise SystemExit(f"{path}: start marker not found")
    end = source.find(end_marker, begin)
    if end < 0:
        raise SystemExit(f"{path}: end marker not found")
    path.write_text(source[:begin] + replacement + "\n\n" + source[end:])


# Preserve early-clobber as target-neutral operand metadata. The RV64 allocator
# is conservative (distinct registers for untied operands), but Core must not
# erase the source constraint semantics.
replace_once(
    "src/core/core_ir.h",
    """    size_t fixed_register_binding_id;\n    bool has_fixed_register_binding;\n} MinicCoreStructuredInlineAsmOperand;\n""",
    """    size_t fixed_register_binding_id;\n    bool has_fixed_register_binding;\n    /* M126A_GENERIC_STRUCTURED_ASM: preserve GCC `&` early-clobber semantics\n       as target-neutral scheduling/allocation metadata. */\n    bool early_clobber;\n} MinicCoreStructuredInlineAsmOperand;\n""",
)

replace_once(
    "src/core/core_ir.c",
    """            if (binding->operand_index > 9U || used_indices[binding->operand_index] ||\n                binding->value >= function->value_count || !available_values[binding->value]) {\n                return false;\n            }\n            used_indices[binding->operand_index] = true;\n            switch (binding->kind) {\n""",
    """            if (binding->operand_index > 9U || used_indices[binding->operand_index] ||\n                binding->value >= function->value_count || !available_values[binding->value] ||\n                (binding->early_clobber &&\n                 binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&\n                 binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE)) {\n                return false;\n            }\n            used_indices[binding->operand_index] = true;\n            switch (binding->kind) {\n""",
)

# Install one canonical role-based structured lowering path before the older
# shape-specific compatibility paths. It accepts only semantics Core can name:
# register output/readwrite, memory output/input/readwrite, and scalar register
# inputs. Immediates/matching constraints/goto remain on their established paths.
generic_lower = r'''
    /* M126A_GENERIC_STRUCTURED_ASM: canonical role lowering for register/memory
       extended asm. This path is intentionally independent of operand counts,
       template spelling, and Linux function names. Target register feasibility
       is deferred to the selected backend. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->label_count == 0U &&
        source->output_count <= MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT &&
        source->input_count <= MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT - source->output_count &&
        source->output_count + source->input_count != 0U &&
        (source->output_count == 0U || source->outputs != NULL) &&
        (source->input_count == 0U || source->inputs != NULL) &&
        source->clobber_count == source->register_clobber_count +
                                     (source->has_memory_clobber ? 1U : 0U)) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t output_index;
        size_t input_index;
        bool supported_shape = true;

        (void)memset(&structured, 0, sizeof(structured));
        structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
        structured.span = statement->span;
        structured.type = minic_type_void();
        structured.result = MINIC_CORE_VALUE_INVALID;
        structured.value.structured_inline_asm.operand_count =
            source->output_count + source->input_count;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicCoreStructuredInlineAsmOperand *binding =
                &structured.value.structured_inline_asm.operands[output_index];
            MinicType value_type;
            MinicCoreLowerStatus status;
            size_t fixed_binding_id;

            if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(expression->type) ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
                break;
            }
            binding->operand_index = output_index;
            binding->early_clobber =
                operand->constraint_text != NULL &&
                memchr(operand->constraint_text, '&', operand->constraint_length) != NULL;
            if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                (core_inline_asm_constraint_is(operand, "=r") ||
                 core_inline_asm_constraint_is(operand, "=&r"))) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       (core_inline_asm_constraint_is(operand, "+r") ||
                        core_inline_asm_constraint_is(operand, "+&r"))) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_constraint_is(operand, "=m")) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
                binding->early_clobber = false;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       (core_inline_asm_constraint_is(operand, "+m") ||
                        core_inline_asm_constraint_is(operand, "+A"))) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
                binding->early_clobber = false;
            } else {
                supported_shape = false;
                break;
            }
            if ((binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                 binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) &&
                core_inline_asm_local_fixed_binding_id(
                    context->body->program, expression, &fixed_binding_id)) {
                binding->fixed_register_binding_id = fixed_binding_id;
                binding->has_fixed_register_binding = true;
            }
            status = lower_address(context, operand->expression, &binding->value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        for (input_index = 0U; supported_shape && input_index < source->input_count;
             ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            size_t operand_index = source->output_count + input_index;
            MinicCoreStructuredInlineAsmOperand *binding =
                &structured.value.structured_inline_asm.operands[operand_index];
            MinicType value_type;
            MinicCoreLowerStatus status;
            size_t fixed_binding_id;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY || expression == NULL) {
                supported_shape = false;
                break;
            }
            binding->operand_index = operand_index;
            if (core_inline_asm_constraint_is(operand, "m")) {
                if (expression->value_category != MINIC_VALUE_LVALUE ||
                    !minic_type_unqualified(expression->type, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
                status = lower_address(context, operand->expression, &binding->value);
            } else if (core_inline_asm_constraint_is(operand, "r") ||
                       core_inline_asm_constraint_is(operand, "rJ") ||
                       core_inline_asm_constraint_is(operand, "Jr") ||
                       core_inline_asm_constraint_is(operand, "rK")) {
                if (!core_scalar_expression_value_type(context->body, expression, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                if (core_inline_asm_local_fixed_binding_id(
                        context->body->program, expression, &fixed_binding_id)) {
                    binding->fixed_register_binding_id = fixed_binding_id;
                    binding->has_fixed_register_binding = true;
                }
                status = lower_expression(context, operand->expression, &binding->value);
            } else {
                supported_shape = false;
                break;
            }
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            size_t clobber_index;
            bool added = minic_core_function_add_opaque_inline_asm(context->function,
                                                                    numeric_template,
                                                                    numeric_template_length,
                                                                    true,
                                                                    source->has_memory_clobber,
                                                                    &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            for (clobber_index = 0U; clobber_index < source->register_clobber_count;
                 ++clobber_index) {
                const MinicInlineAsmRegisterClobber *clobber =
                    &source->register_clobbers[clobber_index];
                if (clobber->name == NULL || clobber->name_length == 0U ||
                    !minic_core_function_add_inline_asm_register_clobber(
                        context->function,
                        inline_asm_id,
                        clobber->name,
                        clobber->name_length)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }
'''
replace_once(
    "src/core/core_lower.c",
    """    if (source == NULL) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n\n    if (core_inline_asm_single_label_goto_supported(context, source)) {\n""",
    """    if (source == NULL) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n""" + generic_lower + """\n    if (core_inline_asm_single_label_goto_supported(context, source)) {\n""",
)

# Target-owned resource allocation. Distinct untied operands are assigned
# distinct registers, which is conservative for early-clobber. Preferences
# preserve the established t-register layouts; a0-a7 provide extra pressure
# capacity. Callee-saved fallback is intentionally left for M126B.
allocator = r'''
typedef struct MinicCoreRiscv64AsmRegisterCandidate {
    const char *name;
} MinicCoreRiscv64AsmRegisterCandidate;

static const MinicCoreRiscv64AsmRegisterCandidate core_asm_caller_saved_registers[] = {
    {"t0"}, {"t1"}, {"t2"}, {"t3"}, {"t4"}, {"t5"}, {"t6"},
    {"a0"}, {"a1"}, {"a2"}, {"a3"}, {"a4"}, {"a5"}, {"a6"}, {"a7"},
};

static bool core_asm_register_name_equal(const char *left, const char *right) {
    return left != NULL && right != NULL && strcmp(left, right) == 0;
}

static bool core_asm_register_is_caller_saved(const char *name) {
    size_t index;
    for (index = 0U;
         index < sizeof(core_asm_caller_saved_registers) /
                     sizeof(core_asm_caller_saved_registers[0]);
         ++index) {
        if (core_asm_register_name_equal(name, core_asm_caller_saved_registers[index].name)) {
            return true;
        }
    }
    return false;
}

static bool core_asm_register_in_use(const char *const *operand_registers,
                                     size_t operand_count,
                                     const char *name) {
    size_t index;
    if (operand_registers == NULL || name == NULL) {
        return true;
    }
    for (index = 0U; index < operand_count; ++index) {
        if (core_asm_register_name_equal(operand_registers[index], name)) {
            return true;
        }
    }
    return false;
}

static const char *core_asm_choose_register(const MinicCoreInlineAsm *inline_asm,
                                            const char *const *operand_registers,
                                            size_t operand_count,
                                            const char *const *preferences,
                                            size_t preference_count) {
    size_t index;
    for (index = 0U; index < preference_count; ++index) {
        const char *candidate = preferences[index];
        if (!core_inline_asm_clobbers_register(inline_asm, candidate) &&
            !core_asm_register_in_use(operand_registers, operand_count, candidate)) {
            return candidate;
        }
    }
    return NULL;
}

static bool core_structured_inline_asm_allocate(
    const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicCoreInstruction *instruction,
    const char **operand_registers,
    bool *memory_operand,
    const char **scratch_register) {
    static const char *const output_preferences[] = {
        "t0", "t1", "t2", "t3", "t4", "t5", "t6",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    };
    static const char *const memory_preferences[] = {
        "t2", "t6", "t5", "t4", "t3", "t1", "t0",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    };
    static const char *const input_preferences[] = {
        "t3", "t4", "t5", "t6", "t2", "t1", "t0",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    };
    const MinicCoreInlineAsm *inline_asm;
    size_t operand_count;
    size_t binding_index;
    size_t clobber_index;

    if (program == NULL || function == NULL || instruction == NULL ||
        operand_registers == NULL || memory_operand == NULL || scratch_register == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    operand_count = instruction->value.structured_inline_asm.operand_count;
    if (operand_count == 0U || operand_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
    for (binding_index = 0U; binding_index < 10U; ++binding_index) {
        operand_registers[binding_index] = NULL;
        memory_operand[binding_index] = false;
    }

    /* M126A remains caller-saved-only. Explicit callee-saved clobbers need
       function-frame preservation and are deliberately deferred to M126B. */
    for (clobber_index = 0U; clobber_index < inline_asm->register_clobber_count;
         ++clobber_index) {
        const MinicCoreInlineAsmRegisterClobber *clobber =
            &inline_asm->register_clobbers[clobber_index];
        if (clobber->name == NULL || !core_asm_register_is_caller_saved(clobber->name)) {
            return false;
        }
    }

    /* Reserve all fixed bindings before generic allocation so source order
       cannot accidentally steal a required architectural register. */
    for (binding_index = 0U; binding_index < operand_count; ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const MinicFixedRegisterBinding *fixed_binding;
        if (!binding->has_fixed_register_binding) {
            continue;
        }
        if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
            binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
            binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {
            return false;
        }
        fixed_binding = minic_c0_program_fixed_register_binding(
            program, binding->fixed_register_binding_id);
        if (fixed_binding == NULL || !fixed_binding->is_local ||
            fixed_binding->register_name == NULL || fixed_binding->register_name_length == 0U ||
            core_inline_asm_clobbers_register(inline_asm, fixed_binding->register_name) ||
            core_asm_register_in_use(operand_registers, 10U, fixed_binding->register_name)) {
            return false;
        }
        operand_registers[binding->operand_index] = fixed_binding->register_name;
    }

    for (binding_index = 0U; binding_index < operand_count; ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const char *register_name;
        const char *const *preferences;
        size_t preference_count;

        if (operand_registers[binding->operand_index] != NULL) {
            continue;
        }
        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            preferences = output_preferences;
            preference_count = sizeof(output_preferences) / sizeof(output_preferences[0]);
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            preferences = memory_preferences;
            preference_count = sizeof(memory_preferences) / sizeof(memory_preferences[0]);
            memory_operand[binding->operand_index] = true;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            preferences = input_preferences;
            preference_count = sizeof(input_preferences) / sizeof(input_preferences[0]);
            break;
        default:
            return false;
        }
        register_name = core_asm_choose_register(
            inline_asm, operand_registers, 10U, preferences, preference_count);
        if (register_name == NULL) {
            return false;
        }
        operand_registers[binding->operand_index] = register_name;
    }

    *scratch_register = NULL;
    for (binding_index = 0U;
         binding_index < sizeof(core_asm_caller_saved_registers) /
                             sizeof(core_asm_caller_saved_registers[0]);
         ++binding_index) {
        const char *candidate = core_asm_caller_saved_registers[binding_index].name;
        /* Scratch is used only before/after the asm, so an asm clobber is fine;
           it merely must not alias a live operand register. */
        if (!core_asm_register_in_use(operand_registers, 10U, candidate)) {
            *scratch_register = candidate;
            break;
        }
    }
    return *scratch_register != NULL;
}
'''
replace_once(
    "src/target/riscv64/core_codegen.c",
    """/* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: the Core model is generic.\n   M68_STRUCTURED_INLINE_ASM_OPTIONAL_INPUTS: this RV64 tier accepts the\n   proven 2 register outputs + 1 read/write memory + 0..2 scalar inputs family. */\n""",
    allocator + "\n/* M126A_GENERIC_STRUCTURED_ASM: capability is now role/resource based. */\n",
)

support_function = r'''static bool core_structured_inline_asm_supported(const MinicC0Program *program,
                                                 const MinicCoreFunction *function,
                                                 const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    const char *operand_registers[10] = {NULL};
    bool memory_operand[10] = {false};
    const char *scratch_register = NULL;
    bool bound[10] = {false};
    size_t binding_index;
    size_t template_index;

    if (program == NULL || function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        instruction->result != MINIC_CORE_VALUE_INVALID || !minic_type_is_void(instruction->type) ||
        instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
        instruction->value.structured_inline_asm.operand_count == 0U ||
        instruction->value.structured_inline_asm.operand_count >
            MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile || inline_asm->is_goto) {
        return false;
    }
    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const MinicFixedRegisterBinding *fixed_binding = NULL;
        MinicType pointee;
        MinicType value_type;

        if (binding->operand_index > 9U || bound[binding->operand_index] ||
            binding->value >= function->value_count ||
            (binding->early_clobber &&
             binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
             binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE)) {
            return false;
        }
        bound[binding->operand_index] = true;
        if (binding->has_fixed_register_binding) {
            fixed_binding = minic_c0_program_fixed_register_binding(
                program, binding->fixed_register_binding_id);
            if (fixed_binding == NULL || !fixed_binding->is_local) {
                return false;
            }
        }
        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type) ||
                (fixed_binding != NULL && !minic_type_equal(fixed_binding->type, value_type))) {
                return false;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            if (fixed_binding != NULL ||
                !minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {
                return false;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            if (!core_scalar_type(function->values[binding->value].type) ||
                (fixed_binding != NULL &&
                 !minic_type_equal(fixed_binding->type, function->values[binding->value].type))) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    for (template_index = 0U; template_index < inline_asm->template_length; ++template_index) {
        unsigned char ch;
        if (inline_asm->template_text[template_index] != '%') {
            continue;
        }
        if (++template_index >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[template_index];
        if (ch == '%') {
            continue;
        }
        if (ch == 'z') {
            if (++template_index >= inline_asm->template_length) {
                return false;
            }
            ch = (unsigned char)inline_asm->template_text[template_index];
        }
        if (ch < '0' || ch > '9' || !bound[(size_t)(ch - '0')]) {
            return false;
        }
    }
    return core_structured_inline_asm_allocate(program,
                                                function,
                                                instruction,
                                                operand_registers,
                                                memory_operand,
                                                &scratch_register);
}'''
replace_function(
    "src/target/riscv64/core_codegen.c",
    "static bool core_structured_inline_asm_supported(",
    "/* M85_RECORD_CALL_ARGUMENT:",
    support_function,
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:\n        return core_structured_inline_asm_supported(function, instruction);\n""",
    """    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:\n        return core_structured_inline_asm_supported(program, function, instruction);\n""",
)

emitter_function = r'''static bool emit_structured_inline_asm(FILE *file,
                                       const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicRiscv64CoreFrame *frame,
                                       const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    const char *operand_registers[10] = {NULL};
    bool memory_operand[10] = {false};
    const char *scratch_register = NULL;
    size_t binding_index;
    size_t index;

    if (file == NULL || program == NULL || frame == NULL ||
        !core_structured_inline_asm_supported(program, function, instruction) ||
        !core_structured_inline_asm_allocate(program,
                                              function,
                                              instruction,
                                              operand_registers,
                                              memory_operand,
                                              &scratch_register)) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];

    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const char *register_name = operand_registers[binding->operand_index];
        MinicType pointee;
        MinicType value_type;

        if (register_name == NULL) {
            return false;
        }
        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            if (!load_core_value(file, frame, binding->value, scratch_register) ||
                !minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) ||
                !minic_riscv64_emit_scalar_load_for_program(
                    file, program, value_type, register_name, scratch_register)) {
                return false;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            if (!load_core_value(file, frame, binding->value, register_name)) {
                return false;
            }
            break;
        default:
            return false;
        }
    }

    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        unsigned char ch;
        size_t operand_index;
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        if (++index >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[index];
        if (ch == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
            continue;
        }
        if (ch == 'z') {
            if (++index >= inline_asm->template_length) {
                return false;
            }
            ch = (unsigned char)inline_asm->template_text[index];
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
        operand_index = (size_t)(ch - '0');
        if (operand_registers[operand_index] == NULL) {
            return false;
        }
        if (memory_operand[operand_index]) {
            if (fprintf(file, "(%s)", operand_registers[operand_index]) < 0) {
                return false;
            }
        } else if (fprintf(file, "%s", operand_registers[operand_index]) < 0) {
            return false;
        }
    }
    if (fputc('\n', file) == EOF) {
        return false;
    }

    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const char *register_name;
        MinicType pointee;
        MinicType value_type;

        if (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
            binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) {
            continue;
        }
        register_name = operand_registers[binding->operand_index];
        if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||
            !minic_type_unqualified(pointee, &value_type) ||
            (minic_type_is_integer(value_type) &&
             !minic_riscv64_emit_integer_conversion_for_program(
                 file, program, value_type, register_name)) ||
            !load_core_value(file, frame, binding->value, scratch_register) ||
            !minic_riscv64_emit_scalar_store_for_program(
                file, program, value_type, register_name, scratch_register)) {
            return false;
        }
    }
    return true;
}'''
replace_function(
    "src/target/riscv64/core_codegen.c",
    "static bool emit_structured_inline_asm(",
    "static bool emit_instruction(",
    emitter_function,
)

# Strict regression for the generic caller-saved allocator and early-clobber
# metadata. Keep the older legacy-emitter tests unchanged.
replace_once(
    "tests/compiler/c0/run-gnu-inline-asm-operands.sh",
    """grep -F 'lw t1, (t2)' \"$work/core-memory-input.s\" >/dev/null\n\ngrep -F 'addi t3, zero, 7' \"$assembly\" >/dev/null\n""",
    r'''grep -F 'lw t1, (t2)' "$work/core-memory-input.s" >/dev/null

cat >"$work/core-generic-structured.c" <<'EOF'
static long output_three_inputs(long a, long b, long c) {
    long out;
    __asm__ __volatile__("add %0, %1, %2\n\txor %0, %0, %3"
                         : "=&r"(out) : "r"(a), "r"(b), "r"(c) : "memory");
    return out;
}

static void three_inputs_a0_clobber(long a, long b, long c) {
    __asm__ __volatile__("add t0, %0, %1\n\txor t0, t0, %2"
                         : : "r"(a), "r"(b), "r"(c) : "a0");
}

static long five_early_outputs(long seed) {
    long a = seed, b = seed, c = seed, d = seed, e = seed;
    __asm__ __volatile__("add %0, %2, %3\n\tadd %1, %4, zero"
                         : "=&r"(a), "=&r"(b), "+&r"(c), "+&r"(d), "+&r"(e)
                         : : "memory");
    return a + b + c + d + e;
}

static long mixed_atomic(long *p, long a, long b, long c, long d) {
    __asm__ __volatile__("amoadd.d %1, %3, %0\n\tadd %2, %2, %4"
                         : "+A"(*p), "+r"(a), "+r"(b) : "r"(c), "r"(d) : "memory");
    return a + b;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/core-generic-structured.c" \
    -o "$work/core-generic-structured.i"
MINIC_CORE_IR=strict "$minic" -S "$work/core-generic-structured.i" \
    -o "$work/core-generic-structured.s"
grep -F 'add t0, t3, t4' "$work/core-generic-structured.s" >/dev/null
grep -F 'xor t0, t0, t5' "$work/core-generic-structured.s" >/dev/null
grep -F 'add t0, t3, t4' "$work/core-generic-structured.s" >/dev/null
grep -F 'amoadd.d t0, t3, (t2)' "$work/core-generic-structured.s" >/dev/null

grep -F 'addi t3, zero, 7' "$assembly" >/dev/null
''',
)

print("M126A generic structured asm staged")
