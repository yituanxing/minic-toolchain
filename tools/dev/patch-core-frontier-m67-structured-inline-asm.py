from pathlib import Path

MARKER = 'M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM'


def replace_once(text: str, anchor: str, replacement: str, label: str) -> str:
    count = text.count(anchor)
    if count != 1:
        raise SystemExit(f'M67 {label} anchor count={count}')
    return text.replace(anchor, replacement, 1)


# ---- Core IR model ---------------------------------------------------------
path = Path('src/core/core_ir.h')
text = path.read_text()
if MARKER not in text:
    text = replace_once(
        text,
        '    MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM,\n    MINIC_CORE_INSTRUCTION_COMPILER_BARRIER,\n',
        '    MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM,\n'
        '    /* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: target-neutral operand bindings. */\n'
        '    MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM,\n'
        '    MINIC_CORE_INSTRUCTION_COMPILER_BARRIER,\n',
        'ir-kind')
    text = replace_once(
        text,
        '''typedef struct MinicCoreInlineAsm {
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool has_memory_clobber;
} MinicCoreInlineAsm;

typedef struct MinicCoreInstruction {
''',
        '''typedef struct MinicCoreInlineAsm {
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool has_memory_clobber;
} MinicCoreInlineAsm;

#define MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT 8U

typedef enum MinicCoreStructuredInlineAsmOperandKind {
    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT = 0,
    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE,
    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
} MinicCoreStructuredInlineAsmOperandKind;

typedef struct MinicCoreStructuredInlineAsmOperand {
    MinicCoreStructuredInlineAsmOperandKind kind;
    size_t operand_index;
    MinicCoreValueId value;
} MinicCoreStructuredInlineAsmOperand;

typedef struct MinicCoreInstruction {
''',
        'ir-binding-types')
    text = replace_once(
        text,
        '''        struct {
            MinicCoreInlineAsmId inline_asm_id;
            MinicCoreValueId operand;
        } scalar_input_inline_asm;
        struct {
            MinicCoreCalleeId callee_id;
''',
        '''        struct {
            MinicCoreInlineAsmId inline_asm_id;
            MinicCoreValueId operand;
        } scalar_input_inline_asm;
        struct {
            MinicCoreInlineAsmId inline_asm_id;
            size_t operand_count;
            MinicCoreStructuredInlineAsmOperand operands[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
        } structured_inline_asm;
        struct {
            MinicCoreCalleeId callee_id;
''',
        'ir-payload')
    path.write_text(text)
else:
    print('M67 core_ir.h already applied')


# ---- Core verifier / dump -------------------------------------------------
path = Path('src/core/core_ir.c')
text = path.read_text()
if MARKER not in text:
    anchor = '''    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_void(instruction->type);
'''
    repl = '''    /* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: Core records operand roles and
       semantic values/addresses; target register assignment stays in the backend. */
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        bool used_indices[10] = {false};
        bool has_memory_readwrite = false;
        size_t operand_index;

        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_void(instruction->type) ||
            instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
            instruction->value.structured_inline_asm.operand_count == 0U ||
            instruction->value.structured_inline_asm.operand_count >
                MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
            return false;
        }
        inline_asm = &function->inline_asms[
            instruction->value.structured_inline_asm.inline_asm_id];
        if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
            !inline_asm->is_volatile) {
            return false;
        }
        for (operand_index = 0U;
             operand_index < instruction->value.structured_inline_asm.operand_count;
             ++operand_index) {
            const MinicCoreStructuredInlineAsmOperand *binding;
            MinicType pointee;
            MinicType value_type;

            binding = &instruction->value.structured_inline_asm.operands[operand_index];
            if (binding->operand_index > 9U || used_indices[binding->operand_index] ||
                binding->value >= function->value_count || !available_values[binding->value]) {
                return false;
            }
            used_indices[binding->operand_index] = true;
            switch (binding->kind) {
            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
                if (!available_pointer_pointee(
                        function, available_values, binding->value, &pointee) ||
                    minic_type_is_const(pointee) ||
                    !minic_type_unqualified(pointee, &value_type) ||
                    (!minic_type_is_integer(value_type) && !minic_type_is_pointer(value_type))) {
                    return false;
                }
                if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {
                    has_memory_readwrite = true;
                }
                break;
            case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
                if (!minic_type_is_integer(function->values[binding->value].type) &&
                    !minic_type_is_pointer(function->values[binding->value].type)) {
                    return false;
                }
                break;
            default:
                return false;
            }
        }
        return !has_memory_readwrite || inline_asm->has_memory_clobber;
    }
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_void(instruction->type);
'''
    text = replace_once(text, anchor, repl, 'verifier')

    anchor = '''    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return fprintf(output, "  compiler.barrier\\n") >= 0;
'''
    repl = '''    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM: {
        MinicCoreInlineAsmId inline_asm_id;
        const MinicCoreInlineAsm *inline_asm;

        inline_asm_id = instruction->value.structured_inline_asm.inline_asm_id;
        if (function == NULL || inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[inline_asm_id];
        return fprintf(output,
                       "  asm.structured id=%" PRIu32 " operands=%zu%s%s\\n",
                       inline_asm_id,
                       instruction->value.structured_inline_asm.operand_count,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return fprintf(output, "  compiler.barrier\\n") >= 0;
'''
    text = replace_once(text, anchor, repl, 'dump')
    path.write_text(text)
else:
    print('M67 core_ir.c already applied')


# ---- Core lowering --------------------------------------------------------
path = Path('src/core/core_lower.c')
text = path.read_text()
if MARKER not in text:
    anchor = '''static MinicCoreLowerStatus lower_opaque_inline_asm(MinicCoreLowerContext *context,
                                                    const MinicStatement *statement) {
'''
    helpers = r'''/* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: normalize GNU named operand
   references to Core's compact numeric operand indices. Constraint semantics
   stay at the lowering boundary; Core itself only retains operand roles. */
static bool core_inline_asm_named_operand_index(const MinicInlineAsm *source,
                                                const char *name,
                                                size_t name_length,
                                                size_t *operand_index) {
    size_t index;

    if (source == NULL || name == NULL || name_length == 0U || operand_index == NULL) {
        return false;
    }
    for (index = 0U; index < source->output_count; ++index) {
        const MinicInlineAsmOperand *operand = &source->outputs[index];
        if (operand->name != NULL && operand->name_length == name_length &&
            memcmp(operand->name, name, name_length) == 0) {
            *operand_index = index;
            return true;
        }
    }
    for (index = 0U; index < source->input_count; ++index) {
        const MinicInlineAsmOperand *operand = &source->inputs[index];
        if (operand->name != NULL && operand->name_length == name_length &&
            memcmp(operand->name, name, name_length) == 0) {
            *operand_index = source->output_count + index;
            return true;
        }
    }
    return false;
}

static bool core_inline_asm_numeric_template(const MinicInlineAsm *source,
                                             char **template_out,
                                             size_t *template_length_out) {
    size_t cursor;
    size_t output_length;
    char *normalized;

    if (source == NULL || template_out == NULL || template_length_out == NULL ||
        source->template_text == NULL || source->template_length == 0U ||
        source->output_count + source->input_count > 10U) {
        return false;
    }
    cursor = 0U;
    output_length = 0U;
    while (cursor < source->template_length) {
        if (source->template_text[cursor] != '%') {
            output_length += 1U;
            cursor += 1U;
            continue;
        }
        if (cursor + 1U >= source->template_length) {
            return false;
        }
        if (source->template_text[cursor + 1U] == '%' ||
            (source->template_text[cursor + 1U] >= '0' &&
             source->template_text[cursor + 1U] <= '9')) {
            size_t numeric_index;
            if (source->template_text[cursor + 1U] != '%') {
                numeric_index = (size_t)(source->template_text[cursor + 1U] - '0');
                if (numeric_index >= source->output_count + source->input_count) {
                    return false;
                }
            }
            output_length += 2U;
            cursor += 2U;
            continue;
        }
        if (source->template_text[cursor + 1U] == '[') {
            size_t name_begin = cursor + 2U;
            size_t name_end = name_begin;
            size_t operand_index;
            while (name_end < source->template_length && source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (name_end >= source->template_length || name_end == name_begin ||
                !core_inline_asm_named_operand_index(source,
                                                     source->template_text + name_begin,
                                                     name_end - name_begin,
                                                     &operand_index) ||
                operand_index > 9U) {
                return false;
            }
            output_length += 2U;
            cursor = name_end + 1U;
            continue;
        }
        return false;
    }
    if (output_length == SIZE_MAX) {
        return false;
    }
    normalized = (char *)malloc(output_length + 1U);
    if (normalized == NULL) {
        return false;
    }
    cursor = 0U;
    output_length = 0U;
    while (cursor < source->template_length) {
        if (source->template_text[cursor] != '%') {
            normalized[output_length++] = source->template_text[cursor++];
            continue;
        }
        normalized[output_length++] = '%';
        if (source->template_text[cursor + 1U] == '%' ||
            (source->template_text[cursor + 1U] >= '0' &&
             source->template_text[cursor + 1U] <= '9')) {
            normalized[output_length++] = source->template_text[cursor + 1U];
            cursor += 2U;
            continue;
        }
        {
            size_t name_begin = cursor + 2U;
            size_t name_end = name_begin;
            size_t operand_index;
            while (source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (!core_inline_asm_named_operand_index(source,
                                                     source->template_text + name_begin,
                                                     name_end - name_begin,
                                                     &operand_index) ||
                operand_index > 9U) {
                free(normalized);
                return false;
            }
            normalized[output_length++] = (char)('0' + operand_index);
            cursor = name_end + 1U;
        }
    }
    normalized[output_length] = '\0';
    *template_out = normalized;
    *template_length_out = output_length;
    return true;
}

static MinicCoreLowerStatus lower_opaque_inline_asm(MinicCoreLowerContext *context,
                                                    const MinicStatement *statement) {
'''
    text = replace_once(text, anchor, helpers, 'lowering-helpers')

    anchor = '''    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
'''
    block = r'''    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 3U && source->input_count == 2U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t output_index;
        size_t input_index;
        size_t register_output_count = 0U;
        size_t memory_output_count = 0U;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                core_inline_asm_register_output_constraint(operand)) {
                const MinicLocal *local;
                if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
                    expression->value_category != MINIC_VALUE_LVALUE ||
                    minic_type_is_const(expression->type) || minic_type_is_volatile(expression->type) ||
                    !minic_type_unqualified(expression->type, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                local = minic_c0_program_local(context->body->program, expression->value.local_id);
                if (local == NULL) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (local->is_array ||
                    minic_c0_program_local_fixed_register_binding(
                        context->body->program, expression->value.local_id) != NULL ||
                    !minic_type_equal(local->type, expression->type)) {
                    supported_shape = false;
                    break;
                }
                register_output_count += 1U;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       core_inline_asm_constraint_is(operand, "+A")) {
                if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
                    minic_type_is_const(expression->type) ||
                    !minic_type_unqualified(expression->type, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                memory_output_count += 1U;
            } else {
                supported_shape = false;
                break;
            }
        }
        for (input_index = 0U; supported_shape && input_index < source->input_count; ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;
            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                !core_inline_asm_constraint_is(operand, "r") || expression == NULL ||
                !core_scalar_expression_value_type(context->body, expression, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
            }
        }
        if (supported_shape && register_output_count == 2U && memory_output_count == 1U &&
            core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added;

            added = minic_core_function_add_opaque_inline_asm(context->function,
                                                               numeric_template,
                                                               numeric_template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id);
            free(numeric_template);
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count =
                source->output_count + source->input_count;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                const MinicInlineAsmOperand *operand = &source->outputs[output_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                MinicCoreLowerStatus status;

                binding->operand_index = output_index;
                binding->kind = operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
                                    : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                const MinicInlineAsmOperand *operand = &source->inputs[input_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[source->output_count + input_index];
                MinicCoreLowerStatus status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = source->output_count + input_index;
                status = lower_expression(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
'''
    text = replace_once(text, anchor, block, 'structured-lowering')
    path.write_text(text)
else:
    print('M67 core_lower.c already applied')


# ---- RV64 structured operand assignment / emission ------------------------
path = Path('src/target/riscv64/core_codegen.c')
text = path.read_text()
if MARKER not in text:
    anchor = '''static bool core_instruction_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
'''
    support = r'''/* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: the Core model is generic;
   this RV64 emission tier currently accepts the proven 2 register outputs +
   1 read/write memory + 2 scalar inputs shape. */
static bool core_structured_inline_asm_supported(const MinicCoreFunction *function,
                                                 const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    bool bound[10] = {false};
    size_t register_outputs = 0U;
    size_t memory_readwrites = 0U;
    size_t scalar_inputs = 0U;
    size_t binding_index;
    size_t template_index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        instruction->result != MINIC_CORE_VALUE_INVALID || !minic_type_is_void(instruction->type) ||
        instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
        instruction->value.structured_inline_asm.operand_count != 5U) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile || !inline_asm->has_memory_clobber) {
        return false;
    }
    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        MinicType pointee;
        MinicType value_type;

        if (binding->operand_index > 9U || bound[binding->operand_index] ||
            binding->value >= function->value_count) {
            return false;
        }
        bound[binding->operand_index] = true;
        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {
                return false;
            }
            register_outputs += 1U;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {
                return false;
            }
            memory_readwrites += 1U;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            if (!core_scalar_type(function->values[binding->value].type)) {
                return false;
            }
            scalar_inputs += 1U;
            break;
        default:
            return false;
        }
    }
    if (register_outputs != 2U || memory_readwrites != 1U || scalar_inputs != 2U) {
        return false;
    }
    for (template_index = 0U; template_index < inline_asm->template_length; ++template_index) {
        unsigned char ch;
        if (inline_asm->template_text[template_index] != '%') {
            continue;
        }
        if (template_index + 1U >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[++template_index];
        if (ch == '%') {
            continue;
        }
        if (ch < '0' || ch > '9' || !bound[(size_t)(ch - '0')]) {
            return false;
        }
    }
    return true;
}

static bool core_instruction_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
'''
    text = replace_once(text, anchor, support, 'rv64-support-helper')
    text = replace_once(
        text,
        '''    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        return core_scalar_input_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
''',
        '''    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        return core_scalar_input_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return core_structured_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
''',
        'rv64-support-switch')

    anchor = '''static bool emit_instruction(FILE *file,
                             const MinicC0Program *program,
                             const MinicCoreFunction *function,
                             const MinicRiscv64CoreFrame *frame,
                             const char *symbol_name,
                             const MinicCoreInstruction *instruction) {
'''
    emitter = r'''static bool emit_structured_inline_asm(FILE *file,
                                       const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicRiscv64CoreFrame *frame,
                                       const MinicCoreInstruction *instruction) {
    static const char *const output_registers[2] = {"t0", "t1"};
    static const char *const memory_registers[1] = {"t2"};
    static const char *const input_registers[2] = {"t3", "t4"};
    const MinicCoreInlineAsm *inline_asm;
    const char *operand_registers[10] = {NULL};
    bool memory_operand[10] = {false};
    size_t output_index = 0U;
    size_t memory_index = 0U;
    size_t input_index = 0U;
    size_t binding_index;
    size_t index;

    if (file == NULL || program == NULL || frame == NULL ||
        !core_structured_inline_asm_supported(function, instruction)) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const char *register_name;

        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            register_name = output_registers[output_index++];
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            register_name = memory_registers[memory_index++];
            memory_operand[binding->operand_index] = true;
            if (!load_core_value(file, frame, binding->value, register_name)) {
                return false;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            register_name = input_registers[input_index++];
            if (!load_core_value(file, frame, binding->value, register_name)) {
                return false;
            }
            break;
        default:
            return false;
        }
        operand_registers[binding->operand_index] = register_name;
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
        MinicType pointee;
        MinicType value_type;
        const char *register_name;

        if (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) {
            continue;
        }
        register_name = operand_registers[binding->operand_index];
        if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||
            !minic_type_unqualified(pointee, &value_type) ||
            (minic_type_is_integer(value_type) &&
             !minic_riscv64_emit_integer_conversion_for_program(
                 file, program, value_type, register_name)) ||
            !load_core_value(file, frame, binding->value, "t5") ||
            !minic_riscv64_emit_scalar_store_for_program(
                file, program, value_type, register_name, "t5")) {
            return false;
        }
    }
    return true;
}

static bool emit_instruction(FILE *file,
                             const MinicC0Program *program,
                             const MinicCoreFunction *function,
                             const MinicRiscv64CoreFrame *frame,
                             const char *symbol_name,
                             const MinicCoreInstruction *instruction) {
'''
    text = replace_once(text, anchor, emitter, 'rv64-emitter')
    text = replace_once(
        text,
        '''    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        return emit_scalar_input_inline_asm(file, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
''',
        '''    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        return emit_scalar_input_inline_asm(file, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return emit_structured_inline_asm(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
''',
        'rv64-emit-switch')
    path.write_text(text)
else:
    print('M67 core_codegen.c already applied')

print('M67 structured multi-operand inline asm applied')
