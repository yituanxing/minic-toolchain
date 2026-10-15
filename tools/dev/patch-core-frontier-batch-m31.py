#!/usr/bin/env python3
from pathlib import Path

def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))

def insert_before(path: str, marker: str, text_to_insert: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(marker)
    if count != 1:
        raise SystemExit(f"{path}: marker count={count}, expected 1: {marker[:120]!r}")
    p.write_text(text.replace(marker, text_to_insert + marker, 1))

replace_once(
    "src/core/core_ir.h",
    "typedef uint32_t MinicCoreInlineAsmId;\n",
    "typedef uint32_t MinicCoreInlineAsmId;\n"
    "typedef uint32_t MinicCoreFixedRegisterId;\n",
)
replace_once(
    "src/core/core_ir.h",
    "#define MINIC_CORE_INLINE_ASM_INVALID UINT32_MAX\n",
    "#define MINIC_CORE_INLINE_ASM_INVALID UINT32_MAX\n"
    "#define MINIC_CORE_FIXED_REGISTER_INVALID UINT32_MAX\n",
)
replace_once(
    "src/core/core_ir.h",
    "    MINIC_CORE_INSTRUCTION_STORE,\n"
    "    MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM,\n",
    "    MINIC_CORE_INSTRUCTION_STORE,\n"
    "    MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ,\n"
    "    MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM,\n",
)
replace_once(
    "src/core/core_ir.h",
    "typedef struct MinicCoreInlineAsm {\n"
    "    char *template_text;\n"
    "    size_t template_length;\n"
    "    bool is_volatile;\n"
    "    bool has_memory_clobber;\n"
    "} MinicCoreInlineAsm;\n",
    r'''typedef struct MinicCoreFixedRegister {
    char *register_name;
    size_t register_name_length;
    MinicType type;
    bool is_local;
} MinicCoreFixedRegister;

typedef enum MinicCoreInlineAsmOperandAccess {
    MINIC_CORE_INLINE_ASM_OPERAND_READ_ONLY = 0,
    MINIC_CORE_INLINE_ASM_OPERAND_WRITE_ONLY,
    MINIC_CORE_INLINE_ASM_OPERAND_READ_WRITE
} MinicCoreInlineAsmOperandAccess;

typedef struct MinicCoreInlineAsmOperand {
    char *constraint_text;
    size_t constraint_length;
    MinicCoreValueId value;
    MinicCoreInlineAsmOperandAccess access;
    bool is_output;
} MinicCoreInlineAsmOperand;

typedef struct MinicCoreInlineAsm {
    char *template_text;
    size_t template_length;
    MinicCoreInlineAsmOperand *operands;
    size_t operand_count;
    size_t operand_capacity;
    size_t output_count;
    size_t input_count;
    bool is_volatile;
    bool has_memory_clobber;
} MinicCoreInlineAsm;
''',
)
replace_once(
    "src/core/core_ir.h",
    "        MinicCoreInlineAsmId inline_asm_id;\n",
    "        MinicCoreFixedRegisterId fixed_register_id;\n"
    "        MinicCoreInlineAsmId inline_asm_id;\n",
)
replace_once(
    "src/core/core_ir.h",
    "    MinicCoreInlineAsm *inline_asms;\n",
    "    MinicCoreFixedRegister *fixed_registers;\n"
    "    size_t fixed_register_count;\n"
    "    size_t fixed_register_capacity;\n"
    "    MinicCoreInlineAsm *inline_asms;\n",
)
replace_once(
    "src/core/core_ir.h",
    "bool minic_core_function_add_opaque_inline_asm(MinicCoreFunction *function,\n",
    r'''bool minic_core_function_add_fixed_register(MinicCoreFunction *function,
                                            const char *register_name,
                                            size_t register_name_length,
                                            MinicType type,
                                            bool is_local,
                                            MinicCoreFixedRegisterId *fixed_register_id);
bool minic_core_function_add_opaque_inline_asm(MinicCoreFunction *function,
''',
)
replace_once(
    "src/core/core_ir.h",
    "bool minic_core_function_append_call_arguments(MinicCoreFunction *function,\n",
    r'''bool minic_core_function_add_inline_asm_operand(MinicCoreFunction *function,
                                                MinicCoreInlineAsmId inline_asm_id,
                                                const char *constraint_text,
                                                size_t constraint_length,
                                                MinicCoreValueId value,
                                                MinicCoreInlineAsmOperandAccess access,
                                                bool is_output);
bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
''',
)

replace_once(
    "src/core/core_ir.c",
    "    size_t inline_asm_index;\n",
    "    size_t fixed_register_index;\n"
    "    size_t inline_asm_index;\n",
)
replace_once(
    "src/core/core_ir.c",
    "    for (inline_asm_index = 0U; inline_asm_index < function->inline_asm_count; ++inline_asm_index) {\n"
    "        free(function->inline_asms[inline_asm_index].template_text);\n"
    "    }\n",
    r'''    for (fixed_register_index = 0U; fixed_register_index < function->fixed_register_count;
         ++fixed_register_index) {
        free(function->fixed_registers[fixed_register_index].register_name);
    }
    for (inline_asm_index = 0U; inline_asm_index < function->inline_asm_count; ++inline_asm_index) {
        size_t operand_index;

        free(function->inline_asms[inline_asm_index].template_text);
        for (operand_index = 0U;
             operand_index < function->inline_asms[inline_asm_index].operand_count;
             ++operand_index) {
            free(function->inline_asms[inline_asm_index].operands[operand_index].constraint_text);
        }
        free(function->inline_asms[inline_asm_index].operands);
    }
''',
)
replace_once(
    "src/core/core_ir.c",
    "    free(function->inline_asms);\n",
    "    free(function->fixed_registers);\n"
    "    free(function->inline_asms);\n",
)
replace_once(
    "src/core/core_ir.c",
    "    for (index = 0U; index < function->inline_asm_count; ++index) {\n"
    "        const MinicCoreInlineAsm *inline_asm;\n\n"
    "        inline_asm = &function->inline_asms[index];\n"
    "        if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||\n"
    "            !inline_asm->is_volatile) {\n"
    "            return false;\n"
    "        }\n"
    "    }\n",
    "",
)

insert_before(
    "src/core/core_ir.c",
    "bool minic_core_function_add_opaque_inline_asm(MinicCoreFunction *function,\n",
    r'''bool minic_core_function_add_fixed_register(MinicCoreFunction *function,
                                            const char *register_name,
                                            size_t register_name_length,
                                            MinicType type,
                                            bool is_local,
                                            MinicCoreFixedRegisterId *fixed_register_id) {
    MinicCoreFixedRegister stored;
    size_t index;

    if (function == NULL || register_name == NULL || register_name_length == 0U ||
        fixed_register_id == NULL || function->fixed_register_count >= (size_t)UINT32_MAX ||
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type))) {
        return false;
    }
    for (index = 0U; index < function->fixed_register_count; ++index) {
        const MinicCoreFixedRegister *existing = &function->fixed_registers[index];

        if (existing->register_name_length == register_name_length &&
            memcmp(existing->register_name, register_name, register_name_length) == 0 &&
            minic_type_equal(existing->type, type) && existing->is_local == is_local) {
            *fixed_register_id = (MinicCoreFixedRegisterId)index;
            return true;
        }
    }
    (void)memset(&stored, 0, sizeof(stored));
    stored.register_name = copy_name(register_name, register_name_length);
    if (stored.register_name == NULL ||
        !grow_array((void **)&function->fixed_registers,
                    &function->fixed_register_capacity,
                    function->fixed_register_count,
                    sizeof(*function->fixed_registers))) {
        free(stored.register_name);
        return false;
    }
    stored.register_name_length = register_name_length;
    stored.type = type;
    stored.is_local = is_local;
    function->fixed_registers[function->fixed_register_count] = stored;
    *fixed_register_id = (MinicCoreFixedRegisterId)function->fixed_register_count;
    function->fixed_register_count += 1U;
    return true;
}

''',
)
replace_once(
    "src/core/core_ir.c",
    "    if (function == NULL || template_text == NULL || template_length == 0U ||\n"
    "        template_length == SIZE_MAX || inline_asm_id == NULL || !is_volatile ||\n"
    "        function->inline_asm_count >= (size_t)UINT32_MAX) {\n"
    "        return false;\n"
    "    }\n"
    "    (void)memset(&stored, 0, sizeof(stored));\n"
    "    stored.template_text = copy_name(template_text, template_length);\n",
    r'''    if (function == NULL || template_text == NULL || template_length == SIZE_MAX ||
        inline_asm_id == NULL || !is_volatile ||
        function->inline_asm_count >= (size_t)UINT32_MAX ||
        (template_length == 0U && !has_memory_clobber)) {
        return false;
    }
    (void)memset(&stored, 0, sizeof(stored));
    stored.template_text = (char *)malloc(template_length + 1U);
    if (stored.template_text != NULL) {
        if (template_length != 0U) {
            (void)memcpy(stored.template_text, template_text, template_length);
        }
        stored.template_text[template_length] = '\0';
    }
''',
)
insert_before(
    "src/core/core_ir.c",
    "bool minic_core_function_append_call_arguments(MinicCoreFunction *function,\n",
    r'''bool minic_core_function_add_inline_asm_operand(MinicCoreFunction *function,
                                                MinicCoreInlineAsmId inline_asm_id,
                                                const char *constraint_text,
                                                size_t constraint_length,
                                                MinicCoreValueId value,
                                                MinicCoreInlineAsmOperandAccess access,
                                                bool is_output) {
    MinicCoreInlineAsm *inline_asm;
    MinicCoreInlineAsmOperand stored;

    if (function == NULL || inline_asm_id >= function->inline_asm_count ||
        constraint_text == NULL || constraint_length == 0U || constraint_length == SIZE_MAX ||
        value >= function->value_count ||
        (is_output && access == MINIC_CORE_INLINE_ASM_OPERAND_READ_ONLY) ||
        (!is_output && access != MINIC_CORE_INLINE_ASM_OPERAND_READ_ONLY)) {
        return false;
    }
    inline_asm = &function->inline_asms[inline_asm_id];
    if (is_output && inline_asm->input_count != 0U) {
        return false;
    }
    (void)memset(&stored, 0, sizeof(stored));
    stored.constraint_text = copy_name(constraint_text, constraint_length);
    if (stored.constraint_text == NULL ||
        !grow_array((void **)&inline_asm->operands,
                    &inline_asm->operand_capacity,
                    inline_asm->operand_count,
                    sizeof(*inline_asm->operands))) {
        free(stored.constraint_text);
        return false;
    }
    stored.constraint_length = constraint_length;
    stored.value = value;
    stored.access = access;
    stored.is_output = is_output;
    inline_asm->operands[inline_asm->operand_count] = stored;
    inline_asm->operand_count += 1U;
    if (is_output) {
        inline_asm->output_count += 1U;
    } else {
        inline_asm->input_count += 1U;
    }
    return true;
}

''',
)
replace_once(
    "src/core/core_ir.c",
    "    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {\n"
    "        const MinicCoreInlineAsm *inline_asm;\n\n"
    "        if (instruction->result != MINIC_CORE_VALUE_INVALID ||\n"
    "            !minic_type_is_void(instruction->type) ||\n"
    "            instruction->value.inline_asm_id >= function->inline_asm_count) {\n"
    "            return false;\n"
    "        }\n"
    "        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n"
    "        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&\n"
    "               inline_asm->is_volatile;\n"
    "    }\n",
    r'''    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegister *binding;

        if (!instruction_result_is_valid(function, instruction) ||
            instruction->value.fixed_register_id >= function->fixed_register_count) {
            return false;
        }
        binding = &function->fixed_registers[instruction->value.fixed_register_id];
        return binding->register_name != NULL && binding->register_name_length != 0U &&
               minic_type_equal(binding->type, instruction->type) &&
               (minic_type_is_integer(binding->type) || minic_type_is_pointer(binding->type));
    }
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        size_t operand_index;

        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_void(instruction->type) ||
            instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        if (inline_asm->template_text == NULL || !inline_asm->is_volatile ||
            inline_asm->output_count + inline_asm->input_count != inline_asm->operand_count ||
            !storage_shape_is_valid(
                inline_asm->operands, inline_asm->operand_count, inline_asm->operand_capacity) ||
            (inline_asm->template_length == 0U &&
             (inline_asm->operand_count != 0U || !inline_asm->has_memory_clobber))) {
            return false;
        }
        for (operand_index = 0U; operand_index < inline_asm->operand_count; ++operand_index) {
            const MinicCoreInlineAsmOperand *operand = &inline_asm->operands[operand_index];

            if (operand->constraint_text == NULL || operand->constraint_length == 0U ||
                operand->value >= function->value_count || !available_values[operand->value]) {
                return false;
            }
            if (operand->is_output) {
                if (operand_index >= inline_asm->output_count ||
                    operand->access == MINIC_CORE_INLINE_ASM_OPERAND_READ_ONLY ||
                    !minic_type_is_pointer(function->values[operand->value].type)) {
                    return false;
                }
            } else if (operand_index < inline_asm->output_count ||
                       operand->access != MINIC_CORE_INLINE_ASM_OPERAND_READ_ONLY ||
                       (!minic_type_is_integer(function->values[operand->value].type) &&
                        !minic_type_is_pointer(function->values[operand->value].type))) {
                return false;
            }
        }
        return true;
    }
''',
)
replace_once(
    "src/core/core_ir.c",
    "        !storage_shape_is_valid(\n"
    "            function->callees, function->callee_count, function->callee_capacity) ||\n"
    "        !storage_shape_is_valid(\n"
    "            function->inline_asms, function->inline_asm_count, function->inline_asm_capacity) ||\n",
    "        !storage_shape_is_valid(\n"
    "            function->callees, function->callee_count, function->callee_capacity) ||\n"
    "        !storage_shape_is_valid(function->fixed_registers,\n"
    "                                function->fixed_register_count,\n"
    "                                function->fixed_register_capacity) ||\n"
    "        !storage_shape_is_valid(\n"
    "            function->inline_asms, function->inline_asm_count, function->inline_asm_capacity) ||\n",
)
insert_before(
    "src/core/core_ir.c",
    "    for (index = 0U; index < function->callee_count; ++index) {\n",
    r'''    for (index = 0U; index < function->fixed_register_count; ++index) {
        const MinicCoreFixedRegister *binding = &function->fixed_registers[index];

        if (binding->register_name == NULL || binding->register_name_length == 0U ||
            (!minic_type_is_integer(binding->type) && !minic_type_is_pointer(binding->type))) {
            return false;
        }
    }
''',
)
replace_once(
    "src/core/core_ir.c",
    "    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {\n"
    "        const MinicCoreInlineAsm *inline_asm;\n\n"
    "        if (function == NULL || instruction->value.inline_asm_id >= function->inline_asm_count) {\n"
    "            return false;\n"
    "        }\n"
    "        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n"
    "        return fprintf(output,\n"
    "                       \"  asm.opaque id=%\" PRIu32 \"%s%s\\n\",\n"
    "                       instruction->value.inline_asm_id,\n"
    "                       inline_asm->is_volatile ? \" volatile\" : \"\",\n"
    "                       inline_asm->has_memory_clobber ? \" memory\" : \"\") >= 0;\n"
    "    }\n",
    r'''    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:
        return fprintf(output,
                       "  %%%" PRIu32 " = fixed_register.read id=%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.fixed_register_id) >= 0;
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;

        if (function == NULL || instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        return fprintf(output,
                       "  asm.opaque id=%" PRIu32 " outputs=%zu inputs=%zu%s%s\n",
                       instruction->value.inline_asm_id,
                       inline_asm->output_count,
                       inline_asm->input_count,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
''',
)

replace_once(
    "src/core/core_lower.c",
    "    if (local->is_array || local->is_register_storage ||\n"
    "        (!core_memory_scalar_type(local->type) && !minic_type_is_record(local->type))) {\n"
    "        return MINIC_CORE_LOWER_UNSUPPORTED;\n"
    "    }\n",
    r'''    if (local->is_array ||
        minic_c0_program_local_fixed_register_binding(context->body->program, local_id) != NULL ||
        (!core_memory_scalar_type(local->type) && !minic_type_is_record(local->type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
''',
)
replace_once(
    "src/core/core_lower.c",
    "        if (minic_type_is_volatile(parameter->type) || parameter->is_array ||\n"
    "            parameter->is_register_storage ||\n",
    "        if (minic_type_is_volatile(parameter->type) || parameter->is_array ||\n"
    "            minic_c0_program_local_fixed_register_binding(context->body->program, local_id) != NULL ||\n",
)
replace_once(
    "src/core/core_lower.c",
    "    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n"
    "        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND) {\n"
    "        MinicCoreBlockId false_block;\n",
    "    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n"
    "        (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||\n"
    "         expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR)) {\n"
    "        MinicCoreBlockId false_block;\n",
)
insert_before(
    "src/core/core_lower.c",
    "    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {\n",
    r'''    if (expression->kind == MINIC_EXPRESSION_FIXED_REGISTER) {
        const MinicFixedRegisterBinding *binding;
        MinicCoreFixedRegisterId fixed_register_id;

        binding = minic_c0_program_fixed_register_binding(
            context->body->program, expression->value.fixed_register_binding_id);
        if (binding == NULL || binding->register_name == NULL || binding->register_name_length == 0U ||
            !core_memory_scalar_type(expression->type) ||
            !minic_type_equal(binding->type, expression->type) || context->target == NULL ||
            (binding->is_local
                 ? !minic_target_info_local_fixed_register_supported(
                       context->target, binding->register_name, binding->register_name_length)
                 : !minic_target_info_fixed_register_supported(
                       context->target, binding->register_name, binding->register_name_length)) ||
            !minic_core_function_add_fixed_register(context->function,
                                                    binding->register_name,
                                                    binding->register_name_length,
                                                    binding->type,
                                                    binding->is_local,
                                                    &fixed_register_id)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.fixed_register_id = fixed_register_id;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
''',
)
old_opaque = r'''    if (!source->is_volatile || source->is_goto || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 0U || source->input_count != 0U ||
        source->label_count != 0U || source->register_clobber_count != 0U) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                   source->template_text,
                                                   source->template_length,
                                                   source->is_volatile,
                                                   source->has_memory_clobber,
                                                   &inline_asm_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }
'''
new_opaque = r'''    if (!source->is_volatile || source->is_goto || source->template_text == NULL ||
        source->label_count != 0U || source->register_clobber_count != 0U ||
        (source->template_length == 0U &&
         (source->output_count != 0U || source->input_count != 0U || !source->has_memory_clobber))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                   source->template_text,
                                                   source->template_length,
                                                   source->is_volatile,
                                                   source->has_memory_clobber,
                                                   &inline_asm_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (source->output_count != 0U || source->input_count != 0U) {
        size_t operand_count;
        MinicCoreObjectId *spill_objects;
        MinicType *spill_types;
        MinicSourceSpan *spill_spans;
        MinicCoreInlineAsmOperandAccess *spill_access;
        const char **constraint_texts;
        size_t *constraint_lengths;
        bool *is_output;
        size_t operand_index;
        MinicCoreLowerStatus status;

        if (source->output_count > SIZE_MAX - source->input_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        operand_count = source->output_count + source->input_count;
        spill_objects = (MinicCoreObjectId *)malloc(operand_count * sizeof(*spill_objects));
        spill_types = (MinicType *)malloc(operand_count * sizeof(*spill_types));
        spill_spans = (MinicSourceSpan *)malloc(operand_count * sizeof(*spill_spans));
        spill_access =
            (MinicCoreInlineAsmOperandAccess *)malloc(operand_count * sizeof(*spill_access));
        constraint_texts = (const char **)malloc(operand_count * sizeof(*constraint_texts));
        constraint_lengths = (size_t *)malloc(operand_count * sizeof(*constraint_lengths));
        is_output = (bool *)malloc(operand_count * sizeof(*is_output));
        if (spill_objects == NULL || spill_types == NULL || spill_spans == NULL ||
            spill_access == NULL || constraint_texts == NULL || constraint_lengths == NULL ||
            is_output == NULL) {
            free(spill_objects);
            free(spill_types);
            free(spill_spans);
            free(spill_access);
            free(constraint_texts);
            free(constraint_lengths);
            free(is_output);
            return MINIC_CORE_LOWER_ERROR;
        }

        status = MINIC_CORE_LOWER_OK;
        for (operand_index = 0U; status == MINIC_CORE_LOWER_OK && operand_index < operand_count;
             ++operand_index) {
            const MinicInlineAsmOperand *operand;
            const MinicExpression *operand_expression;
            MinicCoreValueId operand_value;

            if (operand_index < source->output_count) {
                operand = &source->outputs[operand_index];
                is_output[operand_index] = true;
                spill_access[operand_index] =
                    operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY
                        ? MINIC_CORE_INLINE_ASM_OPERAND_WRITE_ONLY
                        : operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE
                              ? MINIC_CORE_INLINE_ASM_OPERAND_READ_WRITE
                              : MINIC_CORE_INLINE_ASM_OPERAND_READ_ONLY;
                operand_expression =
                    minic_c0_program_expression(context->body->program, operand->expression);
                if (operand_expression == NULL ||
                    operand_expression->value_category != MINIC_VALUE_LVALUE) {
                    status = MINIC_CORE_LOWER_UNSUPPORTED;
                    break;
                }
                status = lower_address(context, operand->expression, &operand_value);
            } else {
                operand = &source->inputs[operand_index - source->output_count];
                is_output[operand_index] = false;
                spill_access[operand_index] = MINIC_CORE_INLINE_ASM_OPERAND_READ_ONLY;
                operand_expression =
                    minic_c0_program_expression(context->body->program, operand->expression);
                if (operand_expression == NULL || !core_memory_scalar_type(operand_expression->type)) {
                    status = MINIC_CORE_LOWER_UNSUPPORTED;
                    break;
                }
                status = lower_expression(context, operand->expression, &operand_value);
            }
            if (status != MINIC_CORE_LOWER_OK) {
                break;
            }
            if (operand_value >= context->function->value_count ||
                !core_memory_scalar_type(context->function->values[operand_value].type)) {
                status = MINIC_CORE_LOWER_UNSUPPORTED;
                break;
            }
            spill_types[operand_index] = context->function->values[operand_value].type;
            spill_spans[operand_index] = operand_expression->span;
            constraint_texts[operand_index] = operand->constraint_text;
            constraint_lengths[operand_index] = operand->constraint_length;
            if (constraint_texts[operand_index] == NULL || constraint_lengths[operand_index] == 0U) {
                status = MINIC_CORE_LOWER_UNSUPPORTED;
                break;
            }
            status = spill_scalar_value(context,
                                        spill_spans[operand_index],
                                        spill_types[operand_index],
                                        operand_value,
                                        &spill_objects[operand_index]);
        }
        for (operand_index = 0U; status == MINIC_CORE_LOWER_OK && operand_index < operand_count;
             ++operand_index) {
            MinicCoreValueId operand_value;

            status = reload_scalar_value(context,
                                         spill_spans[operand_index],
                                         spill_types[operand_index],
                                         spill_objects[operand_index],
                                         &operand_value);
            if (status == MINIC_CORE_LOWER_OK &&
                !minic_core_function_add_inline_asm_operand(context->function,
                                                            inline_asm_id,
                                                            constraint_texts[operand_index],
                                                            constraint_lengths[operand_index],
                                                            operand_value,
                                                            spill_access[operand_index],
                                                            is_output[operand_index])) {
                status = MINIC_CORE_LOWER_ERROR;
            }
        }
        free(spill_objects);
        free(spill_types);
        free(spill_spans);
        free(spill_access);
        free(constraint_texts);
        free(constraint_lengths);
        free(is_output);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
'''
replace_once("src/core/core_lower.c", old_opaque, new_opaque)

replace_once(
    "src/target/riscv64/core_codegen.c",
    "#include <stdint.h>\n#include <stdio.h>\n",
    "#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n",
)

insert_before(
    "src/target/riscv64/core_codegen.c",
    "static bool core_opaque_inline_asm_supported(const MinicCoreFunction *function,\n",
    r'''static bool core_fixed_register_supported(const MinicCoreFunction *function,
                                          const MinicCoreInstruction *instruction) {
    const MinicCoreFixedRegister *binding;
    const MinicTargetInfo *target;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ ||
        instruction->value.fixed_register_id >= function->fixed_register_count) {
        return false;
    }
    binding = &function->fixed_registers[instruction->value.fixed_register_id];
    target = minic_default_target_info();
    return binding->register_name != NULL && binding->register_name_length != 0U &&
           minic_type_equal(binding->type, instruction->type) &&
           (binding->is_local
                ? minic_target_info_local_fixed_register_supported(
                      target, binding->register_name, binding->register_name_length)
                : minic_target_info_fixed_register_supported(
                      target, binding->register_name, binding->register_name_length));
}

static bool core_inline_asm_constraint_is(const MinicCoreInlineAsmOperand *operand,
                                          const char *text) {
    size_t length;

    if (operand == NULL || text == NULL || operand->constraint_text == NULL) {
        return false;
    }
    length = strlen(text);
    return operand->constraint_length == length &&
           memcmp(operand->constraint_text, text, length) == 0;
}

static bool core_inline_asm_operand_supported(const MinicCoreFunction *function,
                                              const MinicCoreInlineAsmOperand *operand) {
    MinicType pointee;

    if (function == NULL || operand == NULL || operand->value >= function->value_count) {
        return false;
    }
    if (operand->is_output) {
        if (!minic_type_pointee(function->values[operand->value].type, &pointee) ||
            (!minic_type_is_integer(pointee) && !minic_type_is_pointer(pointee))) {
            return false;
        }
        if ((core_inline_asm_constraint_is(operand, "=r") ||
             core_inline_asm_constraint_is(operand, "=&r")) &&
            operand->access == MINIC_CORE_INLINE_ASM_OPERAND_WRITE_ONLY) {
            return true;
        }
        if ((core_inline_asm_constraint_is(operand, "+r") ||
             core_inline_asm_constraint_is(operand, "+&r") ||
             core_inline_asm_constraint_is(operand, "+A")) &&
            operand->access == MINIC_CORE_INLINE_ASM_OPERAND_READ_WRITE) {
            return true;
        }
        return false;
    }
    return operand->access == MINIC_CORE_INLINE_ASM_OPERAND_READ_ONLY &&
           core_inline_asm_constraint_is(operand, "r") &&
           (minic_type_is_integer(function->values[operand->value].type) ||
            minic_type_is_pointer(function->values[operand->value].type));
}

static bool core_inline_asm_template_supported(const MinicCoreInlineAsm *inline_asm) {
    size_t index;

    if (inline_asm == NULL || inline_asm->template_text == NULL) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        unsigned char ch;

        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[index + 1U];
        if (ch == '%') {
            index += 1U;
            continue;
        }
        if (ch < '0' || ch > '9' || (size_t)(ch - '0') >= inline_asm->operand_count) {
            return false;
        }
        index += 1U;
    }
    return true;
}

''',
)
replace_once(
    "src/target/riscv64/core_codegen.c",
    "    const MinicCoreInlineAsm *inline_asm;\n\n"
    "    if (function == NULL || instruction == NULL ||\n"
    "        instruction->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||\n"
    "        instruction->value.inline_asm_id >= function->inline_asm_count) {\n"
    "        return false;\n"
    "    }\n"
    "    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n"
    "    return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&\n"
    "           inline_asm->is_volatile;\n"
    "}\n",
    r'''    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||
        instruction->value.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (inline_asm->template_text == NULL || !inline_asm->is_volatile ||
        inline_asm->operand_count > 6U ||
        inline_asm->output_count + inline_asm->input_count != inline_asm->operand_count ||
        (inline_asm->template_length == 0U &&
         (inline_asm->operand_count != 0U || !inline_asm->has_memory_clobber)) ||
        !core_inline_asm_template_supported(inline_asm)) {
        return false;
    }
    for (index = 0U; index < inline_asm->operand_count; ++index) {
        if (!core_inline_asm_operand_supported(function, &inline_asm->operands[index])) {
            return false;
        }
    }
    return true;
}
''',
)
replace_once(
    "src/target/riscv64/core_codegen.c",
    "    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:\n"
    "        return core_opaque_inline_asm_supported(function, instruction);\n",
    "    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:\n"
    "        return core_fixed_register_supported(function, instruction);\n"
    "    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:\n"
    "        return core_opaque_inline_asm_supported(function, instruction);\n",
)
replace_once(
    "src/target/riscv64/core_codegen.c",
    "static bool emit_opaque_inline_asm(FILE *file,\n"
    "                                   const MinicCoreFunction *function,\n"
    "                                   const MinicCoreInstruction *instruction) {\n"
    "    const MinicCoreInlineAsm *inline_asm;\n"
    "    size_t index;\n\n"
    "    if (file == NULL || !core_opaque_inline_asm_supported(function, instruction)) {\n"
    "        return false;\n"
    "    }\n"
    "    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n"
    "    if (fprintf(file, \"  \") < 0) {\n"
    "        return false;\n"
    "    }\n"
    "    for (index = 0U; index < inline_asm->template_length; ++index) {\n"
    "        if (inline_asm->template_text[index] != '%') {\n"
    "            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {\n"
    "                return false;\n"
    "            }\n"
    "            continue;\n"
    "        }\n"
    "        if (index + 1U >= inline_asm->template_length ||\n"
    "            inline_asm->template_text[index + 1U] != '%') {\n"
    "            return false;\n"
    "        }\n"
    "        if (fputc('%', file) == EOF) {\n"
    "            return false;\n"
    "        }\n"
    "        index += 1U;\n"
    "    }\n"
    "    return fputc('\\n', file) != EOF;\n"
    "}\n",
    r'''static const char *const minic_core_rv64_inline_asm_registers[6] = {
    "t0", "t1", "t2", "t3", "t4", "t5",
};

static bool emit_opaque_inline_asm(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicCoreFunction *function,
                                   const MinicRiscv64CoreFrame *frame,
                                   const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || frame == NULL ||
        !core_opaque_inline_asm_supported(function, instruction)) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];

    for (index = 0U; index < inline_asm->operand_count; ++index) {
        const MinicCoreInlineAsmOperand *operand = &inline_asm->operands[index];
        const char *reg = minic_core_rv64_inline_asm_registers[index];

        if (operand->is_output) {
            MinicType pointee;

            if (!minic_type_pointee(function->values[operand->value].type, &pointee)) {
                return false;
            }
            if (core_inline_asm_constraint_is(operand, "+A")) {
                if (!load_core_value(file, frame, operand->value, reg)) {
                    return false;
                }
            } else if (operand->access == MINIC_CORE_INLINE_ASM_OPERAND_READ_WRITE) {
                if (!load_core_value(file, frame, operand->value, "t6") ||
                    !minic_riscv64_emit_scalar_load_for_program(file, program, pointee, reg, "t6")) {
                    return false;
                }
            }
        } else if (!load_core_value(file, frame, operand->value, reg)) {
            return false;
        }
    }

    if (inline_asm->template_length != 0U) {
        if (fprintf(file, "  ") < 0) {
            return false;
        }
        for (index = 0U; index < inline_asm->template_length; ++index) {
            unsigned char ch;

            if (inline_asm->template_text[index] != '%') {
                if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                    return false;
                }
                continue;
            }
            ch = (unsigned char)inline_asm->template_text[index + 1U];
            if (ch == '%') {
                if (fputc('%', file) == EOF) {
                    return false;
                }
            } else {
                size_t operand_index = (size_t)(ch - '0');
                const MinicCoreInlineAsmOperand *operand = &inline_asm->operands[operand_index];
                const char *reg = minic_core_rv64_inline_asm_registers[operand_index];

                if (core_inline_asm_constraint_is(operand, "+A")) {
                    if (fprintf(file, "(%s)", reg) < 0) {
                        return false;
                    }
                } else if (fputs(reg, file) == EOF) {
                    return false;
                }
            }
            index += 1U;
        }
        if (fputc('\n', file) == EOF) {
            return false;
        }
    }

    for (index = 0U; index < inline_asm->output_count; ++index) {
        const MinicCoreInlineAsmOperand *operand = &inline_asm->operands[index];
        const char *reg = minic_core_rv64_inline_asm_registers[index];
        MinicType pointee;

        if (core_inline_asm_constraint_is(operand, "+A")) {
            continue;
        }
        if (!minic_type_pointee(function->values[operand->value].type, &pointee) ||
            !load_core_value(file, frame, operand->value, "t6") ||
            !minic_riscv64_emit_scalar_store_for_program(file, program, pointee, reg, "t6")) {
            return false;
        }
    }
    return true;
}
''',
)
replace_once(
    "src/target/riscv64/core_codegen.c",
    "    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:\n"
    "        return emit_opaque_inline_asm(file, function, instruction);\n",
    r'''    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegister *binding;

        binding = &function->fixed_registers[instruction->value.fixed_register_id];
        if (fprintf(file, "  mv t0, %s\n", binding->register_name) < 0) {
            return false;
        }
        if (minic_type_is_integer(instruction->type) &&
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        return emit_opaque_inline_asm(file, program, function, frame, instruction);
''',
)

print("M31_BATCH_PATCH_APPLIED")
