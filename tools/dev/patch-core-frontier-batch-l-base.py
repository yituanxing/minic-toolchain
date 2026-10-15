#!/usr/bin/env python3
from pathlib import Path

def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: anchor count={count}")
    return text.replace(old, new, 1)

# --- Core IR model ---------------------------------------------------------
path = Path("src/core/core_ir.h")
text = path.read_text()

text = replace_once(
    text,
    '''typedef struct MinicCoreInlineAsm {
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool has_memory_clobber;
''',
    '''typedef struct MinicCoreInlineAsmRegisterClobber {
    char *name;
    size_t name_length;
} MinicCoreInlineAsmRegisterClobber;

typedef struct MinicCoreInlineAsm {
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool has_memory_clobber;
    /* BATCH_L_STRUCTURED_REGISTER_READWRITE: keep register-clobber spelling
       as opaque target metadata. Core does not interpret register names; the
       selected backend only uses them to avoid operand/clobber collisions. */
    MinicCoreInlineAsmRegisterClobber *register_clobbers;
    size_t register_clobber_count;
    size_t register_clobber_capacity;
''',
    "core_ir.h inline asm clobber storage",
)

text = replace_once(
    text,
    '''typedef enum MinicCoreStructuredInlineAsmOperandKind {
    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT = 0,
    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE,
    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
} MinicCoreStructuredInlineAsmOperandKind;
''',
    '''typedef enum MinicCoreStructuredInlineAsmOperandKind {
    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT = 0,
    /* A register read/write operand is address-backed: load the lvalue before
       asm, bind one target register, then store the post-asm value back. */
    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE,
    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE,
    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
} MinicCoreStructuredInlineAsmOperandKind;
''',
    "core_ir.h readwrite enum",
)

text = replace_once(
    text,
    '''bool minic_core_function_add_opaque_inline_asm(MinicCoreFunction *function,
                                               const char *template_text,
                                               size_t template_length,
                                               bool is_volatile,
                                               bool has_memory_clobber,
                                               MinicCoreInlineAsmId *inline_asm_id);
''',
    '''bool minic_core_function_add_opaque_inline_asm(MinicCoreFunction *function,
                                               const char *template_text,
                                               size_t template_length,
                                               bool is_volatile,
                                               bool has_memory_clobber,
                                               MinicCoreInlineAsmId *inline_asm_id);
bool minic_core_function_add_inline_asm_register_clobber(
    MinicCoreFunction *function,
    MinicCoreInlineAsmId inline_asm_id,
    const char *name,
    size_t name_length);
''',
    "core_ir.h clobber API",
)
path.write_text(text)

path = Path("src/core/core_ir.c")
text = path.read_text()

text = replace_once(
    text,
    '''    for (inline_asm_index = 0U; inline_asm_index < function->inline_asm_count; ++inline_asm_index) {
        free(function->inline_asms[inline_asm_index].template_text);
    }
''',
    '''    for (inline_asm_index = 0U; inline_asm_index < function->inline_asm_count; ++inline_asm_index) {
        MinicCoreInlineAsm *inline_asm = &function->inline_asms[inline_asm_index];
        size_t clobber_index;

        free(inline_asm->template_text);
        for (clobber_index = 0U; clobber_index < inline_asm->register_clobber_count;
             ++clobber_index) {
            free(inline_asm->register_clobbers[clobber_index].name);
        }
        free(inline_asm->register_clobbers);
    }
''',
    "core_ir.c destroy clobbers",
)

insert_api = r'''
bool minic_core_function_add_inline_asm_register_clobber(
    MinicCoreFunction *function,
    MinicCoreInlineAsmId inline_asm_id,
    const char *name,
    size_t name_length) {
    MinicCoreInlineAsm *inline_asm;
    MinicCoreInlineAsmRegisterClobber *stored;
    char *name_copy;

    if (function == NULL || inline_asm_id >= function->inline_asm_count ||
        name == NULL || name_length == 0U || name_length == SIZE_MAX) {
        return false;
    }
    inline_asm = &function->inline_asms[inline_asm_id];
    name_copy = copy_name(name, name_length);
    if (name_copy == NULL ||
        !grow_array((void **)&inline_asm->register_clobbers,
                    &inline_asm->register_clobber_capacity,
                    inline_asm->register_clobber_count,
                    sizeof(*inline_asm->register_clobbers))) {
        free(name_copy);
        return false;
    }
    stored = &inline_asm->register_clobbers[inline_asm->register_clobber_count++];
    stored->name = name_copy;
    stored->name_length = name_length;
    return true;
}

'''
anchor = 'bool minic_core_function_append_call_arguments(MinicCoreFunction *function,\n'
if insert_api not in text:
    if text.count(anchor) != 1:
        raise SystemExit(f"core_ir.c clobber API anchor count={text.count(anchor)}")
    text = text.replace(anchor, insert_api + anchor, 1)

text = replace_once(
    text,
    '''            switch (binding->kind) {
            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
''',
    '''            switch (binding->kind) {
            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
''',
    "core_ir.c verifier readwrite",
)
path.write_text(text)

# --- Lowering --------------------------------------------------------------
path = Path("src/core/core_lower.c")
text = path.read_text()

batch_l = r'''    /* BATCH_L_STRUCTURED_REGISTER_READWRITE: after compile-time i/I inputs
       are specialized into target text, preserve a +r operand as one
       address-backed read/write register binding. Register-clobber names stay
       opaque in Core and are interpreted only by the target backend when it
       chooses operand registers. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count != 0U &&
        source->label_count == 0U && !source->has_memory_clobber &&
        source->clobber_count == source->register_clobber_count) {
        const MinicInlineAsmOperand *output = &source->outputs[0];
        const MinicExpression *output_expression =
            minic_c0_program_expression(context->body->program, output->expression);
        MinicType output_type;
        char *specialized_template = NULL;
        size_t specialized_length = 0U;

        if (output->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
            core_inline_asm_constraint_is(output, "+r") &&
            output_expression != NULL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type) &&
            core_inline_asm_specialize_register_output_immediates(
                context, source, &specialized_template, &specialized_length)) {
            MinicCoreInstruction structured;
            MinicCoreStructuredInlineAsmOperand *binding;
            MinicCoreLowerStatus status;
            size_t clobber_index;
            bool added;

            added = minic_core_function_add_opaque_inline_asm(context->function,
                                                               specialized_template,
                                                               specialized_length,
                                                               true,
                                                               false,
                                                               &inline_asm_id);
            free(specialized_template);
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

            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 1U;
            binding = &structured.value.structured_inline_asm.operands[0];
            binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
            binding->operand_index = 0U;
            status = lower_address(context, output->expression, &binding->value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(specialized_template);
    }

'''
anchor = '    /* BATCH_I_REGISTER_OUTPUT_IMMEDIATE_SPECIALIZATION: after all i/I\n'
if batch_l not in text:
    if text.count(anchor) != 1:
        raise SystemExit(f"core_lower.c Batch L anchor count={text.count(anchor)}")
    text = text.replace(anchor, batch_l + anchor, 1)
path.write_text(text)

# --- RV64 structured backend ----------------------------------------------
path = Path("src/target/riscv64/core_codegen.c")
text = path.read_text()

if "#include <string.h>\n" not in text:
    text = replace_once(
        text,
        "#include <stdio.h>\n",
        "#include <stdio.h>\n#include <string.h>\n",
        "core_codegen.c string header",
    )

helper = r'''static bool core_inline_asm_clobbers_register(const MinicCoreInlineAsm *inline_asm,
                                               const char *register_name) {
    size_t index;
    size_t name_length;

    if (inline_asm == NULL || register_name == NULL) {
        return true;
    }
    name_length = strlen(register_name);
    for (index = 0U; index < inline_asm->register_clobber_count; ++index) {
        const MinicCoreInlineAsmRegisterClobber *clobber =
            &inline_asm->register_clobbers[index];
        if (clobber->name != NULL && clobber->name_length == name_length &&
            memcmp(clobber->name, register_name, name_length) == 0) {
            return true;
        }
    }
    return false;
}

'''
anchor = '/* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: the Core model is generic.\n'
if helper not in text:
    if text.count(anchor) != 1:
        raise SystemExit(f"core_codegen.c clobber helper anchor count={text.count(anchor)}")
    text = text.replace(anchor, helper + anchor, 1)

text = replace_once(
    text,
    '''    size_t register_outputs = 0U;
    size_t memory_readwrites = 0U;
    size_t scalar_inputs = 0U;
''',
    '''    size_t register_outputs = 0U;
    size_t register_readwrites = 0U;
    size_t memory_readwrites = 0U;
    size_t scalar_inputs = 0U;
''',
    "core_codegen.c readwrite counter",
)

text = replace_once(
    text,
    '''    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile || !inline_asm->has_memory_clobber) {
        return false;
    }
''',
    '''    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile) {
        return false;
    }
''',
    "core_codegen.c structured clobber-independent header",
)

text = replace_once(
    text,
    '''        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {
                return false;
            }
            register_outputs += 1U;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
''',
    '''        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {
                return false;
            }
            if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) {
                register_readwrites += 1U;
            } else {
                register_outputs += 1U;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
''',
    "core_codegen.c validator readwrite case",
)

text = replace_once(
    text,
    '''    if (!((register_outputs == 2U && memory_readwrites == 1U && scalar_inputs <= 2U &&
           scalar_inputs + 3U == instruction->value.structured_inline_asm.operand_count) ||
          (register_outputs == 0U && memory_readwrites == 0U && scalar_inputs == 2U &&
           instruction->value.structured_inline_asm.operand_count == 2U))) {
        return false;
    }
''',
    '''    if (!((register_outputs == 2U && register_readwrites == 0U &&
           memory_readwrites == 1U && scalar_inputs <= 2U &&
           scalar_inputs + 3U == instruction->value.structured_inline_asm.operand_count &&
           inline_asm->has_memory_clobber) ||
          (register_outputs == 0U && register_readwrites == 0U &&
           memory_readwrites == 0U && scalar_inputs == 2U &&
           instruction->value.structured_inline_asm.operand_count == 2U &&
           inline_asm->has_memory_clobber) ||
          (register_outputs == 0U && register_readwrites == 1U &&
           memory_readwrites == 0U && scalar_inputs == 0U &&
           instruction->value.structured_inline_asm.operand_count == 1U &&
           !inline_asm->has_memory_clobber))) {
        return false;
    }
    if (register_readwrites == 1U &&
        core_inline_asm_clobbers_register(inline_asm, "t0") &&
        core_inline_asm_clobbers_register(inline_asm, "t1")) {
        return false;
    }
''',
    "core_codegen.c accepted shapes",
)

text = replace_once(
    text,
    '''        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            register_name = output_registers[output_index++];
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
''',
    '''        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE: {
            MinicType pointee;
            MinicType value_type;

            while (output_index < 2U &&
                   core_inline_asm_clobbers_register(
                       inline_asm, output_registers[output_index])) {
                output_index += 1U;
            }
            if (output_index >= 2U) {
                return false;
            }
            register_name = output_registers[output_index++];
            if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) {
                if (!load_core_value(file, frame, binding->value, "t5") ||
                    !minic_type_pointee(function->values[binding->value].type, &pointee) ||
                    !minic_type_unqualified(pointee, &value_type) ||
                    !core_scalar_type(value_type) ||
                    !minic_riscv64_emit_scalar_load_for_program(
                        file, program, value_type, register_name, "t5")) {
                    return false;
                }
            }
            break;
        }
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
''',
    "core_codegen.c emitter preload readwrite",
)

text = replace_once(
    text,
    '''        if (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) {
            continue;
        }
''',
    '''        if (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
            binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) {
            continue;
        }
''',
    "core_codegen.c emitter poststore readwrite",
)
path.write_text(text)

print("CORE_BATCH_L_PATCHED structured +r immediate asm with opaque register clobbers")
