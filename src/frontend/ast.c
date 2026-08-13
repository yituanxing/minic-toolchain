#include "frontend/ast.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool minic_grow_array(void **data, size_t *capacity, size_t count, size_t element_size) {
    void *resized;
    size_t new_capacity;

    if (count < *capacity) {
        return true;
    }

    new_capacity = *capacity == 0U ? 16U : *capacity * 2U;
    if (new_capacity < *capacity || new_capacity > SIZE_MAX / element_size) {
        return false;
    }

    resized = realloc(*data, new_capacity * element_size);
    if (resized == NULL) {
        return false;
    }
    *data = resized;
    *capacity = new_capacity;
    return true;
}

static char *minic_copy_name(const char *name, size_t name_length) {
    char *copy;

    if (name == NULL || name_length == SIZE_MAX) {
        return NULL;
    }
    copy = (char *)malloc(name_length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    (void)memcpy(copy, name, name_length);
    copy[name_length] = '\0';
    return copy;
}

void minic_c0_program_initialize(MinicC0Program *program) {
    (void)memset(program, 0, sizeof(*program));
    program->body_block = MINIC_BLOCK_INVALID;
    program->entry_function = MINIC_FUNCTION_INVALID;
    program->return_expression = MINIC_EXPRESSION_INVALID;
}

void minic_c0_program_destroy(MinicC0Program *program) {
    size_t index;

    for (index = 0U; index < program->block_count; ++index) {
        free(program->blocks[index].statements);
    }
    for (index = 0U; index < program->file_asm_count; ++index) {
        free(program->file_asms[index].text);
    }
    for (index = 0U; index < program->inline_asm_count; ++index) {
        size_t clobber_index;
        size_t operand_index;

        free(program->inline_asms[index].template_text);
        for (operand_index = 0U; operand_index < program->inline_asms[index].output_count;
             ++operand_index) {
            free(program->inline_asms[index].outputs[operand_index].name);
            free(program->inline_asms[index].outputs[operand_index].constraint_text);
        }
        for (operand_index = 0U; operand_index < program->inline_asms[index].input_count;
             ++operand_index) {
            free(program->inline_asms[index].inputs[operand_index].name);
            free(program->inline_asms[index].inputs[operand_index].constraint_text);
        }
        for (operand_index = 0U; operand_index < program->inline_asms[index].label_count;
             ++operand_index) {
            free(program->inline_asms[index].labels[operand_index].name);
        }
        for (clobber_index = 0U; clobber_index < program->inline_asms[index].register_clobber_count;
             ++clobber_index) {
            free(program->inline_asms[index].register_clobbers[clobber_index].name);
        }
        free(program->inline_asms[index].outputs);
        free(program->inline_asms[index].inputs);
        free(program->inline_asms[index].labels);
        free(program->inline_asms[index].register_clobbers);
    }
    for (index = 0U; index < program->function_count; ++index) {
        free(program->functions[index].name);
        free(program->functions[index].assembler_name);
        free(program->functions[index].section_name);
    }
    for (index = 0U; index < program->record_count; ++index) {
        MinicRecord *record;
        size_t field_index;

        record = &program->records[index];
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            free(record->fields[field_index].name);
        }
        free(record->fields);
        free(record->name);
    }
    for (index = 0U; index < program->type_alias_count; ++index) {
        free(program->type_aliases[index].name);
    }
    for (index = 0U; index < program->enum_count; ++index) {
        free(program->enums[index].name);
    }
    for (index = 0U; index < program->enumerator_count; ++index) {
        free(program->enumerators[index].name);
    }
    for (index = 0U; index < program->fixed_register_binding_count; ++index) {
        free(program->fixed_register_bindings[index].name);
        free(program->fixed_register_bindings[index].register_name);
    }
    for (index = 0U; index < program->global_object_count; ++index) {
        free(program->global_objects[index].name);
        free(program->global_objects[index].section_name);
        free(program->global_objects[index].initializer_values);
        free(program->global_objects[index].relocations);
    }
    free(program->expressions);
    free(program->locals);
    free(program->cleanup_contexts);
    free(program->statements);
    free(program->inline_asms);
    free(program->file_asms);
    free(program->blocks);
    free(program->functions);
    free(program->records);
    free(program->array_types);
    free(program->function_types);
    free(program->type_aliases);
    free(program->enums);
    free(program->enumerators);
    free(program->global_objects);
    free(program->fixed_register_bindings);
    minic_c0_program_initialize(program);
}

static void
minic_refresh_enum_type(MinicType *type, MinicEnumId enum_id, MinicType compatible_type) {
    if (type != NULL && type->base_kind == MINIC_TYPE_BASE_ENUM && type->enum_id == enum_id) {
        type->integer_sign = compatible_type.integer_sign;
        type->integer_rank = compatible_type.integer_rank;
    }
}

static void minic_refresh_program_enum_types(MinicC0Program *program,
                                             MinicEnumId enum_id,
                                             MinicType compatible_type) {
    size_t index;

    for (index = 0U; index < program->expression_count; ++index) {
        minic_refresh_enum_type(&program->expressions[index].type, enum_id, compatible_type);
        if (program->expressions[index].kind == MINIC_EXPRESSION_SIZEOF) {
            minic_refresh_enum_type(
                &program->expressions[index].value.sizeof_type, enum_id, compatible_type);
        }
    }
    for (index = 0U; index < program->local_count; ++index) {
        minic_refresh_enum_type(&program->locals[index].type, enum_id, compatible_type);
    }
    for (index = 0U; index < program->function_count; ++index) {
        size_t parameter_index;

        minic_refresh_enum_type(&program->functions[index].return_type, enum_id, compatible_type);
        for (parameter_index = 0U; parameter_index < program->functions[index].parameter_count;
             ++parameter_index) {
            minic_refresh_enum_type(&program->functions[index].parameter_types[parameter_index],
                                    enum_id,
                                    compatible_type);
        }
    }
    for (index = 0U; index < program->record_count; ++index) {
        size_t field_index;

        for (field_index = 0U; field_index < program->records[index].field_count; ++field_index) {
            minic_refresh_enum_type(
                &program->records[index].fields[field_index].type, enum_id, compatible_type);
        }
    }
    for (index = 0U; index < program->array_type_count; ++index) {
        minic_refresh_enum_type(
            &program->array_types[index].element_type, enum_id, compatible_type);
    }
    for (index = 0U; index < program->function_type_count; ++index) {
        size_t parameter_index;

        minic_refresh_enum_type(
            &program->function_types[index].return_type, enum_id, compatible_type);
        for (parameter_index = 0U; parameter_index < program->function_types[index].parameter_count;
             ++parameter_index) {
            minic_refresh_enum_type(
                &program->function_types[index].parameter_types[parameter_index],
                enum_id,
                compatible_type);
        }
    }
    for (index = 0U; index < program->type_alias_count; ++index) {
        minic_refresh_enum_type(&program->type_aliases[index].type, enum_id, compatible_type);
    }
    for (index = 0U; index < program->global_object_count; ++index) {
        minic_refresh_enum_type(&program->global_objects[index].type, enum_id, compatible_type);
    }
    for (index = 0U; index < program->fixed_register_binding_count; ++index) {
        minic_refresh_enum_type(
            &program->fixed_register_bindings[index].type, enum_id, compatible_type);
    }
}

bool minic_c0_program_add_enum(MinicC0Program *program,
                               const char *name,
                               size_t name_length,
                               MinicEnumId *enum_id) {
    MinicEnum entity;

    if (program == NULL || enum_id == NULL || ((name == NULL) != (name_length == 0U)) ||
        !minic_grow_array((void **)&program->enums,
                          &program->enum_capacity,
                          program->enum_count,
                          sizeof(*program->enums))) {
        return false;
    }
    (void)memset(&entity, 0, sizeof(entity));
    if (name_length != 0U) {
        entity.name = minic_copy_name(name, name_length);
        if (entity.name == NULL) {
            return false;
        }
        entity.name_length = name_length;
    }
    entity.compatible_type = minic_type_int();
    *enum_id = program->enum_count;
    program->enums[program->enum_count] = entity;
    program->enum_count += 1U;
    return true;
}

bool minic_c0_program_finish_enum(MinicC0Program *program,
                                  MinicEnumId enum_id,
                                  MinicType compatible_type) {
    MinicEnum *entity;

    if (program == NULL || enum_id >= program->enum_count ||
        !minic_type_is_integer(compatible_type) ||
        compatible_type.base_kind != MINIC_TYPE_BASE_INT || compatible_type.pointer_depth != 0U) {
        return false;
    }
    entity = &program->enums[enum_id];
    if (entity->is_complete) {
        return false;
    }
    entity->compatible_type = compatible_type;
    entity->is_complete = true;
    minic_refresh_program_enum_types(program, enum_id, compatible_type);
    return true;
}

bool minic_c0_program_add_enumerator(MinicC0Program *program,
                                     MinicEnumId enum_id,
                                     const char *name,
                                     size_t name_length,
                                     MinicType type,
                                     uint64_t bits,
                                     MinicEnumeratorId *enumerator_id) {
    MinicEnumerator enumerator;

    if (program == NULL || enum_id >= program->enum_count || name == NULL || name_length == 0U ||
        enumerator_id == NULL || !minic_type_is_integer(type) || minic_type_is_enum(type) ||
        !minic_grow_array((void **)&program->enumerators,
                          &program->enumerator_capacity,
                          program->enumerator_count,
                          sizeof(*program->enumerators))) {
        return false;
    }
    (void)memset(&enumerator, 0, sizeof(enumerator));
    enumerator.name = minic_copy_name(name, name_length);
    if (enumerator.name == NULL) {
        return false;
    }
    enumerator.name_length = name_length;
    enumerator.enum_id = enum_id;
    enumerator.type = type;
    enumerator.bits = bits;
    *enumerator_id = program->enumerator_count;
    program->enumerators[program->enumerator_count] = enumerator;
    program->enumerator_count += 1U;
    return true;
}

const MinicEnum *minic_c0_program_enum(const MinicC0Program *program, MinicEnumId enum_id) {
    return program != NULL && enum_id < program->enum_count ? &program->enums[enum_id] : NULL;
}

const MinicEnumerator *minic_c0_program_enumerator(const MinicC0Program *program,
                                                   MinicEnumeratorId enumerator_id) {
    return program != NULL && enumerator_id < program->enumerator_count
               ? &program->enumerators[enumerator_id]
               : NULL;
}

bool minic_c0_types_compatible(const MinicC0Program *program, MinicType left, MinicType right) {
    MinicType left_unqualified;
    MinicType right_unqualified;
    const MinicEnum *entity;

    if (!minic_type_unqualified(left, &left_unqualified) ||
        !minic_type_unqualified(right, &right_unqualified)) {
        return minic_type_equal(left, right);
    }
    if (minic_type_is_enum(left_unqualified) && minic_type_is_enum(right_unqualified)) {
        return minic_type_equal(left_unqualified, right_unqualified);
    }
    if (minic_type_is_enum(left_unqualified) &&
        right_unqualified.base_kind == MINIC_TYPE_BASE_INT) {
        entity = minic_c0_program_enum(program, left_unqualified.enum_id);
        return entity != NULL && entity->is_complete &&
               minic_type_equal(entity->compatible_type, right_unqualified);
    }
    if (minic_type_is_enum(right_unqualified) &&
        left_unqualified.base_kind == MINIC_TYPE_BASE_INT) {
        entity = minic_c0_program_enum(program, right_unqualified.enum_id);
        return entity != NULL && entity->is_complete &&
               minic_type_equal(entity->compatible_type, left_unqualified);
    }
    return minic_type_equal(left_unqualified, right_unqualified);
}

bool minic_c0_program_add_expression(MinicC0Program *program,
                                     const MinicExpression *expression,
                                     MinicExpressionId *expression_id) {
    if (!minic_grow_array((void **)&program->expressions,
                          &program->expression_capacity,
                          program->expression_count,
                          sizeof(*program->expressions))) {
        return false;
    }

    *expression_id = program->expression_count;
    program->expressions[program->expression_count] = *expression;
    program->expression_count += 1U;
    return true;
}

bool minic_c0_program_add_local(MinicC0Program *program,
                                const MinicLocal *local,
                                MinicLocalId *local_id) {
    if (!minic_grow_array((void **)&program->locals,
                          &program->local_capacity,
                          program->local_count,
                          sizeof(*program->locals))) {
        return false;
    }

    *local_id = program->local_count;
    program->locals[program->local_count] = *local;
    program->local_count += 1U;
    return true;
}

bool minic_c0_program_add_cleanup_context(MinicC0Program *program,
                                          MinicCleanupContextId parent,
                                          MinicExpressionId cleanup_expression,
                                          MinicCleanupContextId *cleanup_context_id) {
    MinicCleanupContext context;

    if (program == NULL || cleanup_context_id == NULL || parent > program->cleanup_context_count ||
        cleanup_expression >= program->expression_count ||
        !minic_grow_array((void **)&program->cleanup_contexts,
                          &program->cleanup_context_capacity,
                          program->cleanup_context_count,
                          sizeof(*program->cleanup_contexts))) {
        return false;
    }
    context.parent = parent;
    context.cleanup_expression = cleanup_expression;
    program->cleanup_contexts[program->cleanup_context_count] = context;
    program->cleanup_context_count += 1U;
    *cleanup_context_id = program->cleanup_context_count;
    return true;
}

const MinicCleanupContext *
minic_c0_program_cleanup_context(const MinicC0Program *program,
                                 MinicCleanupContextId cleanup_context_id) {
    if (program == NULL || cleanup_context_id == MINIC_CLEANUP_CONTEXT_ROOT ||
        cleanup_context_id > program->cleanup_context_count) {
        return NULL;
    }
    return &program->cleanup_contexts[cleanup_context_id - 1U];
}

bool minic_c0_cleanup_context_reaches(const MinicC0Program *program,
                                      MinicCleanupContextId current,
                                      MinicCleanupContextId stop) {
    if (program == NULL || current > program->cleanup_context_count ||
        stop > program->cleanup_context_count) {
        return false;
    }
    while (current != stop) {
        const MinicCleanupContext *context;

        context = minic_c0_program_cleanup_context(program, current);
        if (context == NULL) {
            return false;
        }
        current = context->parent;
    }
    return true;
}

bool minic_c0_program_add_statement(MinicC0Program *program,
                                    const MinicStatement *statement,
                                    MinicStatementId *statement_id) {
    if (!minic_grow_array((void **)&program->statements,
                          &program->statement_capacity,
                          program->statement_count,
                          sizeof(*program->statements))) {
        return false;
    }

    *statement_id = program->statement_count;
    program->statements[program->statement_count] = *statement;
    program->statement_count += 1U;
    return true;
}

bool minic_c0_program_add_file_asm(MinicC0Program *program, const char *text, size_t length) {
    MinicFileAsm file_asm;

    if (program == NULL || text == NULL || length == SIZE_MAX ||
        memchr(text, '\0', length) != NULL ||
        !minic_grow_array((void **)&program->file_asms,
                          &program->file_asm_capacity,
                          program->file_asm_count,
                          sizeof(*program->file_asms))) {
        return false;
    }
    (void)memset(&file_asm, 0, sizeof(file_asm));
    file_asm.text = minic_copy_name(text, length);
    if (file_asm.text == NULL) {
        return false;
    }
    file_asm.length = length;
    program->file_asms[program->file_asm_count] = file_asm;
    program->file_asm_count += 1U;
    return true;
}

bool minic_c0_program_add_inline_asm(MinicC0Program *program,
                                     const char *template_text,
                                     size_t template_length,
                                     bool is_volatile,
                                     bool has_memory_clobber,
                                     MinicInlineAsmId *inline_asm_id) {
    MinicInlineAsm inline_asm;

    if (program == NULL || template_text == NULL || inline_asm_id == NULL) {
        return false;
    }
    if (!minic_grow_array((void **)&program->inline_asms,
                          &program->inline_asm_capacity,
                          program->inline_asm_count,
                          sizeof(*program->inline_asms))) {
        return false;
    }
    (void)memset(&inline_asm, 0, sizeof(inline_asm));
    inline_asm.template_text = minic_copy_name(template_text, template_length);
    if (inline_asm.template_text == NULL) {
        return false;
    }
    inline_asm.template_length = template_length;
    inline_asm.is_volatile = is_volatile;
    inline_asm.has_memory_clobber = has_memory_clobber;
    inline_asm.clobber_count = has_memory_clobber ? 1U : 0U;
    *inline_asm_id = program->inline_asm_count;
    program->inline_asms[program->inline_asm_count] = inline_asm;
    program->inline_asm_count += 1U;
    return true;
}

static bool inline_asm_operand_name_matches(const MinicInlineAsmOperand *operand,
                                            const char *name,
                                            size_t name_length) {
    return operand != NULL && operand->name != NULL && name != NULL &&
           operand->name_length == name_length && memcmp(operand->name, name, name_length) == 0;
}

static bool inline_asm_operand_name_is_available(const MinicInlineAsm *inline_asm,
                                                 const char *name,
                                                 size_t name_length) {
    size_t index;

    if (inline_asm == NULL || name_length == 0U) {
        return name == NULL && name_length == 0U;
    }
    if (name == NULL) {
        return false;
    }
    for (index = 0U; index < inline_asm->output_count; ++index) {
        if (inline_asm_operand_name_matches(&inline_asm->outputs[index], name, name_length)) {
            return false;
        }
    }
    for (index = 0U; index < inline_asm->input_count; ++index) {
        if (inline_asm_operand_name_matches(&inline_asm->inputs[index], name, name_length)) {
            return false;
        }
    }
    return true;
}

static bool initialize_inline_asm_operand(MinicInlineAsmOperand *operand,
                                          const char *name,
                                          size_t name_length,
                                          const char *constraint_text,
                                          size_t constraint_length,
                                          MinicExpressionId expression,
                                          MinicInlineAsmOperandAccess access) {
    if (operand == NULL || constraint_text == NULL || constraint_length == 0U ||
        ((name == NULL) != (name_length == 0U))) {
        return false;
    }
    (void)memset(operand, 0, sizeof(*operand));
    if (name_length != 0U) {
        operand->name = minic_copy_name(name, name_length);
        if (operand->name == NULL) {
            return false;
        }
        operand->name_length = name_length;
    }
    operand->constraint_text = minic_copy_name(constraint_text, constraint_length);
    if (operand->constraint_text == NULL) {
        free(operand->name);
        operand->name = NULL;
        operand->name_length = 0U;
        return false;
    }
    operand->constraint_length = constraint_length;
    operand->expression = expression;
    operand->access = access;
    return true;
}

bool minic_c0_program_add_inline_asm_output(MinicC0Program *program,
                                            MinicInlineAsmId inline_asm_id,
                                            const char *name,
                                            size_t name_length,
                                            const char *constraint_text,
                                            size_t constraint_length,
                                            MinicExpressionId expression,
                                            MinicInlineAsmOperandAccess access) {
    MinicInlineAsm *inline_asm;
    MinicInlineAsmOperand operand;

    if (program == NULL || inline_asm_id >= program->inline_asm_count || constraint_text == NULL ||
        constraint_length == 0U || expression >= program->expression_count ||
        (access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
         access != MINIC_INLINE_ASM_OPERAND_READ_WRITE)) {
        return false;
    }
    inline_asm = &program->inline_asms[inline_asm_id];
    if (!inline_asm_operand_name_is_available(inline_asm, name, name_length) ||
        !minic_grow_array((void **)&inline_asm->outputs,
                          &inline_asm->output_capacity,
                          inline_asm->output_count,
                          sizeof(*inline_asm->outputs)) ||
        !initialize_inline_asm_operand(
            &operand, name, name_length, constraint_text, constraint_length, expression, access)) {
        return false;
    }
    inline_asm->outputs[inline_asm->output_count] = operand;
    inline_asm->output_count += 1U;
    return true;
}

bool minic_c0_program_add_inline_asm_input(MinicC0Program *program,
                                           MinicInlineAsmId inline_asm_id,
                                           const char *name,
                                           size_t name_length,
                                           const char *constraint_text,
                                           size_t constraint_length,
                                           MinicExpressionId expression) {
    MinicInlineAsm *inline_asm;
    MinicInlineAsmOperand operand;

    if (program == NULL || inline_asm_id >= program->inline_asm_count || constraint_text == NULL ||
        constraint_length == 0U || expression >= program->expression_count) {
        return false;
    }
    inline_asm = &program->inline_asms[inline_asm_id];
    if (!inline_asm_operand_name_is_available(inline_asm, name, name_length) ||
        !minic_grow_array((void **)&inline_asm->inputs,
                          &inline_asm->input_capacity,
                          inline_asm->input_count,
                          sizeof(*inline_asm->inputs)) ||
        !initialize_inline_asm_operand(&operand,
                                       name,
                                       name_length,
                                       constraint_text,
                                       constraint_length,
                                       expression,
                                       MINIC_INLINE_ASM_OPERAND_READ_ONLY)) {
        return false;
    }
    inline_asm->inputs[inline_asm->input_count] = operand;
    inline_asm->input_count += 1U;
    return true;
}

bool minic_c0_program_add_inline_asm_register_clobber(MinicC0Program *program,
                                                      MinicInlineAsmId inline_asm_id,
                                                      const char *name,
                                                      size_t name_length) {
    MinicInlineAsm *inline_asm;
    MinicInlineAsmRegisterClobber clobber;
    size_t index;

    if (program == NULL || inline_asm_id >= program->inline_asm_count || name == NULL ||
        name_length == 0U) {
        return false;
    }
    inline_asm = &program->inline_asms[inline_asm_id];
    for (index = 0U; index < inline_asm->register_clobber_count; ++index) {
        if (inline_asm->register_clobbers[index].name_length == name_length &&
            memcmp(inline_asm->register_clobbers[index].name, name, name_length) == 0) {
            return true;
        }
    }
    if (inline_asm->register_clobber_count == SIZE_MAX ||
        !minic_grow_array((void **)&inline_asm->register_clobbers,
                          &inline_asm->register_clobber_capacity,
                          inline_asm->register_clobber_count,
                          sizeof(*inline_asm->register_clobbers))) {
        return false;
    }
    (void)memset(&clobber, 0, sizeof(clobber));
    clobber.name = minic_copy_name(name, name_length);
    if (clobber.name == NULL) {
        return false;
    }
    clobber.name_length = name_length;
    inline_asm->register_clobbers[inline_asm->register_clobber_count] = clobber;
    inline_asm->register_clobber_count += 1U;
    inline_asm->clobber_count =
        inline_asm->register_clobber_count + (inline_asm->has_memory_clobber ? 1U : 0U);
    return true;
}

bool minic_c0_program_set_inline_asm_memory_clobber(MinicC0Program *program,
                                                    MinicInlineAsmId inline_asm_id,
                                                    bool has_memory_clobber) {
    MinicInlineAsm *inline_asm;

    if (program == NULL || inline_asm_id >= program->inline_asm_count) {
        return false;
    }
    inline_asm = &program->inline_asms[inline_asm_id];
    if (has_memory_clobber && inline_asm->register_clobber_count == SIZE_MAX) {
        return false;
    }
    inline_asm->has_memory_clobber = has_memory_clobber;
    inline_asm->clobber_count = inline_asm->register_clobber_count + (has_memory_clobber ? 1U : 0U);
    return true;
}

bool minic_c0_program_set_inline_asm_goto(MinicC0Program *program,
                                          MinicInlineAsmId inline_asm_id,
                                          bool is_goto) {
    if (program == NULL || inline_asm_id >= program->inline_asm_count) {
        return false;
    }
    program->inline_asms[inline_asm_id].is_goto = is_goto;
    return true;
}

bool minic_c0_program_add_inline_asm_label(MinicC0Program *program,
                                           MinicInlineAsmId inline_asm_id,
                                           const char *name,
                                           size_t name_length,
                                           MinicStatementId target_statement) {
    MinicInlineAsm *inline_asm;
    MinicInlineAsmLabel label;
    size_t index;

    if (program == NULL || inline_asm_id >= program->inline_asm_count || name == NULL ||
        name_length == 0U ||
        (target_statement != MINIC_STATEMENT_INVALID &&
         target_statement >= program->statement_count)) {
        return false;
    }
    inline_asm = &program->inline_asms[inline_asm_id];
    for (index = 0U; index < inline_asm->label_count; ++index) {
        if (inline_asm->labels[index].name_length == name_length &&
            memcmp(inline_asm->labels[index].name, name, name_length) == 0) {
            return false;
        }
    }
    if (!minic_grow_array((void **)&inline_asm->labels,
                          &inline_asm->label_capacity,
                          inline_asm->label_count,
                          sizeof(*inline_asm->labels))) {
        return false;
    }
    (void)memset(&label, 0, sizeof(label));
    label.name = minic_copy_name(name, name_length);
    if (label.name == NULL) {
        return false;
    }
    label.name_length = name_length;
    label.target_statement = target_statement;
    inline_asm->labels[inline_asm->label_count] = label;
    inline_asm->label_count += 1U;
    return true;
}

bool minic_c0_program_add_block(MinicC0Program *program, MinicBlockId *block_id) {
    MinicBlock block;

    if (!minic_grow_array((void **)&program->blocks,
                          &program->block_capacity,
                          program->block_count,
                          sizeof(*program->blocks))) {
        return false;
    }

    (void)memset(&block, 0, sizeof(block));
    *block_id = program->block_count;
    program->blocks[program->block_count] = block;
    program->block_count += 1U;
    return true;
}

bool minic_c0_block_add_statement(MinicC0Program *program,
                                  MinicBlockId block_id,
                                  MinicStatementId statement_id) {
    MinicBlock *block;

    if (block_id >= program->block_count || statement_id >= program->statement_count) {
        return false;
    }

    block = &program->blocks[block_id];
    if (!minic_grow_array((void **)&block->statements,
                          &block->statement_capacity,
                          block->statement_count,
                          sizeof(*block->statements))) {
        return false;
    }

    block->statements[block->statement_count] = statement_id;
    block->statement_count += 1U;
    return true;
}

bool minic_c0_program_add_function(MinicC0Program *program,
                                   const char *name,
                                   size_t name_length,
                                   size_t local_begin,
                                   size_t local_count,
                                   MinicBlockId body_block,
                                   MinicFunctionId *function_id) {
    MinicFunction function;
    size_t parameter_index;

    if (name == NULL || function_id == NULL ||
        (body_block != MINIC_BLOCK_INVALID && body_block >= program->block_count) ||
        local_begin > program->local_count || local_count > program->local_count - local_begin) {
        return false;
    }
    if (!minic_grow_array((void **)&program->functions,
                          &program->function_capacity,
                          program->function_count,
                          sizeof(*program->functions))) {
        return false;
    }

    (void)memset(&function, 0, sizeof(function));
    function.name = minic_copy_name(name, name_length);
    if (function.name == NULL) {
        return false;
    }
    function.name_length = name_length;
    function.return_type = minic_type_int();
    for (parameter_index = 0U;
         parameter_index < sizeof(function.parameter_types) / sizeof(function.parameter_types[0]);
         ++parameter_index) {
        function.parameter_types[parameter_index] = minic_type_int();
    }
    function.local_begin = local_begin;
    function.local_count = local_count;
    function.local_storage_size = 0U;
    function.parameter_count = 0U;
    function.body_block = body_block;
    function.is_defined = body_block != MINIC_BLOCK_INVALID;

    *function_id = program->function_count;
    program->functions[program->function_count] = function;
    program->function_count += 1U;
    return true;
}

bool minic_c0_program_set_function_signature(MinicC0Program *program,
                                             MinicFunctionId function_id,
                                             MinicType return_type,
                                             const MinicType *parameter_types,
                                             size_t parameter_count) {
    MinicFunction *function;
    MinicType normalized_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
    size_t parameter_index;

    if (program == NULL || function_id >= program->function_count ||
        parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (parameter_count != 0U && parameter_types == NULL)) {
        return false;
    }
    for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
        if (minic_type_is_void(parameter_types[parameter_index]) ||
            !minic_type_unqualified(parameter_types[parameter_index],
                                    &normalized_parameter_types[parameter_index])) {
            return false;
        }
    }

    function = &program->functions[function_id];
    if (function->is_defined && (function->local_begin > program->local_count ||
                                 parameter_count > program->local_count - function->local_begin)) {
        return false;
    }
    function->return_type = return_type;
    function->parameter_count = parameter_count;
    for (parameter_index = 0U; parameter_index < MINIC_MAX_FUNCTION_PARAMETERS; ++parameter_index) {
        if (parameter_index < parameter_count) {
            function->parameter_types[parameter_index] =
                normalized_parameter_types[parameter_index];
        } else {
            function->parameter_types[parameter_index] = minic_type_void();
        }
    }
    return true;
}

bool minic_c0_program_set_function_parameter_count(MinicC0Program *program,
                                                   MinicFunctionId function_id,
                                                   size_t parameter_count) {
    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
    size_t parameter_index;

    if (parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
        return false;
    }
    for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
        parameter_types[parameter_index] = minic_type_int();
    }
    return minic_c0_program_set_function_signature(
        program, function_id, minic_type_int(), parameter_types, parameter_count);
}

bool minic_c0_program_define_function(MinicC0Program *program,
                                      MinicFunctionId function_id,
                                      size_t local_begin,
                                      MinicBlockId body_block) {
    MinicFunction *function;

    if (function_id >= program->function_count || body_block >= program->block_count ||
        local_begin > program->local_count) {
        return false;
    }
    function = &program->functions[function_id];
    if (function->is_defined) {
        return false;
    }
    function->local_begin = local_begin;
    function->local_count = 0U;
    function->local_storage_size = 0U;
    function->body_block = body_block;
    function->is_defined = true;
    return true;
}

bool minic_c0_program_finish_function(MinicC0Program *program,
                                      MinicFunctionId function_id,
                                      size_t local_count) {
    MinicFunction *function;

    if (function_id >= program->function_count) {
        return false;
    }
    function = &program->functions[function_id];
    if (!function->is_defined || function->body_block >= program->block_count ||
        function->local_begin > program->local_count ||
        local_count > program->local_count - function->local_begin) {
        return false;
    }
    function->local_count = local_count;
    function->local_storage_size = 0U;
    return true;
}

bool minic_c0_program_add_record(MinicC0Program *program,
                                 const char *name,
                                 size_t name_length,
                                 MinicRecordId *record_id) {
    MinicRecord record;
    size_t index;

    if (program == NULL || name == NULL || name_length == 0U || record_id == NULL) {
        return false;
    }
    for (index = 0U; index < program->record_count; ++index) {
        const MinicRecord *existing;

        existing = &program->records[index];
        if (name_length == existing->name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            return false;
        }
    }
    if (!minic_grow_array((void **)&program->records,
                          &program->record_capacity,
                          program->record_count,
                          sizeof(*program->records))) {
        return false;
    }

    (void)memset(&record, 0, sizeof(record));
    record.name = minic_copy_name(name, name_length);
    if (record.name == NULL) {
        return false;
    }
    record.name_length = name_length;
    *record_id = program->record_count;
    program->records[program->record_count] = record;
    program->record_count += 1U;
    return true;
}

bool minic_c0_program_add_anonymous_record(MinicC0Program *program, MinicRecordId *record_id) {
    MinicRecord record;

    if (program == NULL || record_id == NULL) {
        return false;
    }
    if (!minic_grow_array((void **)&program->records,
                          &program->record_capacity,
                          program->record_count,
                          sizeof(*program->records))) {
        return false;
    }

    (void)memset(&record, 0, sizeof(record));
    record.name = minic_copy_name("", 0U);
    if (record.name == NULL) {
        return false;
    }
    *record_id = program->record_count;
    program->records[program->record_count] = record;
    program->record_count += 1U;
    return true;
}

bool minic_c0_record_add_field(MinicC0Program *program,
                               MinicRecordId record_id,
                               const char *name,
                               size_t name_length,
                               MinicType type,
                               size_t element_count) {
    MinicRecord *record;
    MinicRecordField field;
    size_t index;

    if (program == NULL || record_id >= program->record_count || name == NULL ||
        element_count == 0U) {
        return false;
    }
    record = &program->records[record_id];
    if (record->is_complete) {
        return false;
    }
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *existing;

        existing = &record->fields[index];
        if (name_length != 0U && existing->name_length == name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            return false;
        }
    }
    if (!minic_grow_array((void **)&record->fields,
                          &record->field_capacity,
                          record->field_count,
                          sizeof(*record->fields))) {
        return false;
    }

    (void)memset(&field, 0, sizeof(field));
    field.name = minic_copy_name(name, name_length);
    if (field.name == NULL) {
        return false;
    }
    field.name_length = name_length;
    field.type = type;
    field.element_count = element_count;
    record->fields[record->field_count] = field;
    record->field_count += 1U;
    return true;
}

bool minic_c0_record_add_bit_field(MinicC0Program *program,
                                   MinicRecordId record_id,
                                   const char *name,
                                   size_t name_length,
                                   MinicType type,
                                   size_t bit_width) {
    MinicRecord *record;
    MinicRecordField *field;

    if (program == NULL || record_id >= program->record_count || name == NULL ||
        !minic_type_is_integer(type) || (name_length != 0U && bit_width == 0U) ||
        !minic_c0_record_add_field(program, record_id, name, name_length, type, 1U)) {
        return false;
    }
    record = &program->records[record_id];
    field = &record->fields[record->field_count - 1U];
    field->is_bit_field = true;
    field->bit_width = bit_width;
    field->bit_offset = 0U;
    return true;
}

bool minic_c0_record_add_unnamed_bit_field(MinicC0Program *program,
                                           MinicRecordId record_id,
                                           MinicType type,
                                           size_t bit_width) {
    return minic_c0_record_add_bit_field(program, record_id, "", 0U, type, bit_width);
}

bool minic_c0_program_finish_record(MinicC0Program *program, MinicRecordId record_id) {
    MinicRecord *record;

    if (program == NULL || record_id >= program->record_count) {
        return false;
    }
    record = &program->records[record_id];
    if (record->is_complete) {
        return false;
    }
    record->is_complete = true;
    return true;
}

static bool minic_c0_program_add_array_descriptor(MinicC0Program *program,
                                                  MinicType element_type,
                                                  size_t element_count,
                                                  MinicType *array_type) {
    MinicArrayType descriptor;
    MinicArrayTypeId array_type_id;

    if (program == NULL || array_type == NULL || minic_type_is_void(element_type) ||
        minic_type_is_function(element_type)) {
        return false;
    }
    if (!minic_grow_array((void **)&program->array_types,
                          &program->array_type_capacity,
                          program->array_type_count,
                          sizeof(*program->array_types))) {
        return false;
    }

    (void)memset(&descriptor, 0, sizeof(descriptor));
    descriptor.element_type = element_type;
    descriptor.element_count = element_count;
    array_type_id = program->array_type_count;
    program->array_types[program->array_type_count] = descriptor;
    program->array_type_count += 1U;
    *array_type = minic_type_array(array_type_id);
    return true;
}

bool minic_c0_program_add_array_type(MinicC0Program *program,
                                     MinicType element_type,
                                     size_t element_count,
                                     MinicType *array_type) {
    return element_count != 0U &&
           minic_c0_program_add_array_descriptor(program, element_type, element_count, array_type);
}

bool minic_c0_program_add_incomplete_array_type(MinicC0Program *program,
                                                MinicType element_type,
                                                MinicType *array_type) {
    return minic_c0_program_add_array_descriptor(program, element_type, 0U, array_type);
}

bool minic_c0_program_add_zero_length_array_type(MinicC0Program *program,
                                                 MinicType element_type,
                                                 MinicType *array_type) {
    MinicType created;

    if (program == NULL || array_type == NULL ||
        !minic_c0_program_add_array_descriptor(program, element_type, 0U, &created)) {
        return false;
    }
    program->array_types[created.array_type_id].is_zero_length = true;
    *array_type = created;
    return true;
}

bool minic_c0_program_complete_zero_length_array_type(MinicC0Program *program,
                                                      MinicType array_type) {
    MinicArrayType *descriptor;

    if (program == NULL || !minic_type_is_array(array_type) ||
        array_type.array_type_id >= program->array_type_count) {
        return false;
    }
    descriptor = &program->array_types[array_type.array_type_id];
    if (descriptor->is_zero_length) {
        return descriptor->element_count == 0U;
    }
    if (descriptor->element_count != 0U) {
        return false;
    }
    descriptor->is_zero_length = true;
    return true;
}

bool minic_c0_program_complete_array_type(MinicC0Program *program,
                                          MinicType array_type,
                                          size_t element_count) {
    MinicArrayType *descriptor;

    if (program == NULL || !minic_type_is_array(array_type) || element_count == 0U ||
        array_type.array_type_id >= program->array_type_count) {
        return false;
    }
    descriptor = &program->array_types[array_type.array_type_id];
    if (descriptor->is_zero_length) {
        return false;
    }
    if (descriptor->element_count != 0U) {
        return descriptor->element_count == element_count;
    }
    descriptor->element_count = element_count;
    return true;
}

static bool minic_function_type_matches(const MinicFunctionType *descriptor,
                                        MinicType return_type,
                                        const MinicType *parameter_types,
                                        size_t parameter_count) {
    size_t parameter_index;

    if (descriptor == NULL || descriptor->parameter_count != parameter_count ||
        !minic_type_equal(descriptor->return_type, return_type)) {
        return false;
    }
    for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
        if (!minic_type_equal(descriptor->parameter_types[parameter_index],
                              parameter_types[parameter_index])) {
            return false;
        }
    }
    return true;
}

bool minic_c0_program_add_function_type(MinicC0Program *program,
                                        MinicType return_type,
                                        const MinicType *parameter_types,
                                        size_t parameter_count,
                                        MinicType *function_type) {
    MinicFunctionType descriptor;
    MinicType normalized_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
    size_t function_type_index;
    size_t parameter_index;

    if (program == NULL || function_type == NULL ||
        parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (parameter_count != 0U && parameter_types == NULL) || minic_type_is_array(return_type) ||
        minic_type_is_function(return_type)) {
        return false;
    }
    for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
        if (minic_type_is_void(parameter_types[parameter_index]) ||
            minic_type_is_function(parameter_types[parameter_index]) ||
            !minic_type_unqualified(parameter_types[parameter_index],
                                    &normalized_parameter_types[parameter_index])) {
            return false;
        }
    }
    for (function_type_index = 0U; function_type_index < program->function_type_count;
         ++function_type_index) {
        if (minic_function_type_matches(&program->function_types[function_type_index],
                                        return_type,
                                        normalized_parameter_types,
                                        parameter_count)) {
            *function_type = minic_type_function(function_type_index);
            return true;
        }
    }
    if (!minic_grow_array((void **)&program->function_types,
                          &program->function_type_capacity,
                          program->function_type_count,
                          sizeof(*program->function_types))) {
        return false;
    }

    (void)memset(&descriptor, 0, sizeof(descriptor));
    descriptor.return_type = return_type;
    descriptor.parameter_count = parameter_count;
    for (parameter_index = 0U; parameter_index < MINIC_MAX_FUNCTION_PARAMETERS; ++parameter_index) {
        if (parameter_index < parameter_count) {
            descriptor.parameter_types[parameter_index] =
                normalized_parameter_types[parameter_index];
        } else {
            descriptor.parameter_types[parameter_index] = minic_type_void();
        }
    }
    function_type_index = program->function_type_count;
    program->function_types[program->function_type_count] = descriptor;
    program->function_type_count += 1U;
    *function_type = minic_type_function(function_type_index);
    return true;
}

bool minic_c0_program_add_type_alias(MinicC0Program *program,
                                     const char *name,
                                     size_t name_length,
                                     MinicType type,
                                     MinicTypeAliasId *alias_id) {
    MinicTypeAlias alias;
    size_t index;

    if (program == NULL || name == NULL || alias_id == NULL || minic_type_is_void(type)) {
        return false;
    }
    for (index = 0U; index < program->type_alias_count; ++index) {
        const MinicTypeAlias *existing;

        existing = &program->type_aliases[index];
        if (name_length == existing->name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            return false;
        }
    }
    if (!minic_grow_array((void **)&program->type_aliases,
                          &program->type_alias_capacity,
                          program->type_alias_count,
                          sizeof(*program->type_aliases))) {
        return false;
    }

    (void)memset(&alias, 0, sizeof(alias));
    alias.name = minic_copy_name(name, name_length);
    if (alias.name == NULL) {
        return false;
    }
    alias.name_length = name_length;
    alias.type = type;
    *alias_id = program->type_alias_count;
    program->type_aliases[program->type_alias_count] = alias;
    program->type_alias_count += 1U;
    return true;
}

const MinicExpression *minic_c0_program_expression(const MinicC0Program *program,
                                                   MinicExpressionId expression_id) {
    if (program == NULL || expression_id >= program->expression_count) {
        return NULL;
    }
    return &program->expressions[expression_id];
}

bool minic_c0_expression_array_object_info(const MinicC0Program *program,
                                           const MinicExpression *expression,
                                           MinicArrayObjectInfo *info) {
    MinicArrayObjectInfo resolved;

    if (program == NULL || expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    (void)memset(&resolved, 0, sizeof(resolved));
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;

        local = minic_c0_program_local(program, expression->value.local_id);
        if (local != NULL && local->is_array) {
            resolved.element_type = expression->type;
            resolved.element_count = local->element_count;
            resolved.is_incomplete = local->element_count == 0U;
        } else if (!minic_type_is_array(expression->type)) {
            return false;
        } else {
            const MinicArrayType *array_type;

            array_type = minic_c0_program_array_type(program, expression->type.array_type_id);
            if (array_type == NULL) {
                return false;
            }
            resolved.element_type = array_type->element_type;
            resolved.element_count = array_type->element_count;
            resolved.is_zero_length = array_type->is_zero_length;
            resolved.is_incomplete = array_type->element_count == 0U && !array_type->is_zero_length;
            resolved.has_materialized_type = true;
        }
    } else if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicRecord *record;
        const MinicRecordField *field;

        record = minic_c0_program_record(program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (field != NULL && field->is_array) {
            resolved.element_type = expression->type;
            resolved.element_count = field->element_count;
            resolved.is_incomplete = field->is_flexible_array;
            resolved.is_zero_length = field->is_zero_length_array;
        } else if (!minic_type_is_array(expression->type)) {
            return false;
        } else {
            const MinicArrayType *array_type;

            array_type = minic_c0_program_array_type(program, expression->type.array_type_id);
            if (array_type == NULL) {
                return false;
            }
            resolved.element_type = array_type->element_type;
            resolved.element_count = array_type->element_count;
            resolved.is_zero_length = array_type->is_zero_length;
            resolved.is_incomplete = array_type->element_count == 0U && !array_type->is_zero_length;
            resolved.has_materialized_type = true;
        }
    } else if (minic_type_is_array(expression->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, expression->type.array_type_id);
        if (array_type == NULL) {
            return false;
        }
        resolved.element_type = array_type->element_type;
        resolved.element_count = array_type->element_count;
        resolved.is_incomplete = array_type->element_count == 0U;
        resolved.has_materialized_type = true;
    } else {
        return false;
    }
    if (info != NULL) {
        *info = resolved;
    }
    return true;
}

const MinicRecordField *minic_c0_expression_bit_field(const MinicC0Program *program,
                                                      MinicExpressionId expression_id) {
    const MinicExpression *expression;
    const MinicRecord *record;
    const MinicRecordField *field;

    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_MEMBER) {
        return NULL;
    }
    record = minic_c0_program_record(program, expression->value.member.record_id);
    field = minic_c0_record_field(record, expression->value.member.field_index);
    return field != NULL && field->is_bit_field ? field : NULL;
}

bool minic_c0_record_value_is_address_backed(const MinicC0Program *program,
                                             MinicExpressionId expression_id) {
    size_t remaining;

    if (program == NULL) {
        return false;
    }
    remaining = program->expression_count + 1U;
    while (remaining > 0U) {
        const MinicExpression *expression;
        MinicExpressionId result_id;

        expression = minic_c0_program_expression(program, expression_id);
        if (expression == NULL || !minic_type_is_record(expression->type)) {
            return false;
        }
        if (expression->value_category == MINIC_VALUE_LVALUE) {
            return true;
        }
        if (expression->value_category != MINIC_VALUE_RVALUE ||
            expression->kind != MINIC_EXPRESSION_STATEMENT) {
            return false;
        }
        result_id = expression->value.statement_expression.result;
        if (result_id == MINIC_EXPRESSION_INVALID || result_id >= expression_id) {
            return false;
        }
        expression_id = result_id;
        remaining -= 1U;
    }
    return false;
}

bool minic_c0_record_value_is_copy_source(const MinicC0Program *program,
                                          MinicExpressionId expression_id) {
    const MinicExpression *expression;

    if (program == NULL) {
        return false;
    }
    if (minic_c0_record_value_is_address_backed(program, expression_id)) {
        return true;
    }
    expression = minic_c0_program_expression(program, expression_id);
    return expression != NULL && expression->kind == MINIC_EXPRESSION_CALL &&
           expression->value_category == MINIC_VALUE_RVALUE &&
           minic_type_is_record(expression->type);
}

bool minic_c0_expression_is_null_pointer_constant_v0(const MinicC0Program *program,
                                                     MinicExpressionId expression_id) {
    const MinicExpression *expression;
    const MinicExpression *operand;
    MinicType pointee;

    if (program == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_INTEGER) {
        return minic_type_is_integer(expression->type) && expression->value.integer_value == 0;
    }
    if ((expression->kind != MINIC_EXPRESSION_CAST &&
         expression->kind != MINIC_EXPRESSION_BITCAST) ||
        expression->type.pointer_depth != 1U || !minic_type_pointee(expression->type, &pointee) ||
        !minic_type_is_void(pointee)) {
        return false;
    }
    operand = minic_c0_program_expression(program, expression->value.unary.operand);
    return operand != NULL && operand->kind == MINIC_EXPRESSION_INTEGER &&
           minic_type_is_integer(operand->type) && operand->value.integer_value == 0;
}

static bool
minic_c0_conditional_type_only(MinicType when_true, MinicType when_false, MinicType *result) {
    bool has_double_operand;
    bool has_numeric_operands;

    if (result == NULL) {
        return false;
    }
    if (minic_type_equal(when_true, when_false)) {
        *result = when_true;
        return true;
    }
    if (minic_type_conditional_pointer_common(when_true, when_false, result)) {
        return true;
    }
    if (minic_type_is_integer(when_true) && minic_type_is_integer(when_false)) {
        return minic_type_integer_common(when_true, when_false, result);
    }
    has_double_operand = minic_type_is_double(when_true) || minic_type_is_double(when_false);
    has_numeric_operands = (minic_type_is_double(when_true) || minic_type_is_integer(when_true)) &&
                           (minic_type_is_double(when_false) || minic_type_is_integer(when_false));
    if (has_double_operand && has_numeric_operands) {
        *result = minic_type_double();
        return true;
    }
    return false;
}

bool minic_c0_conditional_result_type(const MinicC0Program *program,
                                      MinicExpressionId when_true_expression_id,
                                      MinicExpressionId when_false_expression_id,
                                      MinicType *result) {
    const MinicExpression *when_true;
    const MinicExpression *when_false;

    if (program == NULL || result == NULL) {
        return false;
    }
    when_true = minic_c0_program_expression(program, when_true_expression_id);
    when_false = minic_c0_program_expression(program, when_false_expression_id);
    if (when_true == NULL || when_false == NULL) {
        return false;
    }
    if (minic_type_is_pointer(when_true->type) &&
        minic_c0_expression_is_null_pointer_constant_v0(program, when_false_expression_id)) {
        *result = when_true->type;
        return true;
    }
    if (minic_c0_expression_is_null_pointer_constant_v0(program, when_true_expression_id) &&
        minic_type_is_pointer(when_false->type)) {
        *result = when_false->type;
        return true;
    }
    return minic_c0_conditional_type_only(when_true->type, when_false->type, result);
}

bool minic_c0_assignment_compatible(const MinicC0Program *program,
                                    MinicType target_type,
                                    MinicExpressionId source_expression_id) {
    const MinicExpression *source;

    if (program == NULL) {
        return false;
    }
    source = minic_c0_program_expression(program, source_expression_id);
    if (source == NULL) {
        return false;
    }
    if (minic_type_assignment_compatible(target_type, source->type)) {
        return true;
    }
    return minic_type_is_pointer(target_type) &&
           minic_c0_expression_is_null_pointer_constant_v0(program, source_expression_id);
}

static bool minic_c0_type_is_complete_object_bounded(const MinicC0Program *program,
                                                     MinicType type,
                                                     size_t remaining_depth) {
    if (program == NULL || remaining_depth == 0U || minic_type_is_void(type) ||
        minic_type_is_function(type)) {
        return false;
    }
    if (minic_type_is_enum(type)) {
        const MinicEnum *entity;

        entity = minic_c0_program_enum(program, type.enum_id);
        return entity != NULL && entity->is_complete;
    }
    if (minic_type_is_integer(type) || minic_type_is_float(type) || minic_type_is_double(type) ||
        minic_type_is_pointer(type)) {
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, type.record_id);
        return record != NULL && record->is_complete;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        return array_type != NULL && array_type->element_count != 0U &&
               minic_c0_type_is_complete_object_bounded(
                   program, array_type->element_type, remaining_depth - 1U);
    }
    return false;
}

bool minic_c0_type_is_complete_object(const MinicC0Program *program, MinicType type) {
    size_t remaining_depth;

    if (program == NULL) {
        return false;
    }
    remaining_depth = program->array_type_count;
    remaining_depth += program->record_count;
    remaining_depth += program->function_type_count;
    remaining_depth += program->enum_count;
    remaining_depth += 1U;
    return minic_c0_type_is_complete_object_bounded(program, type, remaining_depth);
}

bool minic_c0_pointer_arithmetic_pointee_allowed(const MinicC0Program *program,
                                                 MinicType pointee_type) {
    return minic_type_is_void(pointee_type) || minic_type_is_function(pointee_type) ||
           minic_c0_type_is_complete_object(program, pointee_type);
}

bool minic_c0_pointer_relational_compatible(const MinicC0Program *program,
                                            MinicType left,
                                            MinicType right) {
    MinicType left_pointee;
    MinicType right_pointee;
    MinicType left_unqualified;
    MinicType right_unqualified;

    return program != NULL && minic_type_pointee(left, &left_pointee) &&
           minic_type_pointee(right, &right_pointee) &&
           minic_type_unqualified(left_pointee, &left_unqualified) &&
           minic_type_unqualified(right_pointee, &right_unqualified) &&
           minic_type_equal(left_unqualified, right_unqualified) &&
           minic_c0_pointer_arithmetic_pointee_allowed(program, left_unqualified) &&
           minic_c0_pointer_arithmetic_pointee_allowed(program, right_unqualified);
}

bool minic_c0_pointer_equality_compatible(const MinicC0Program *program,
                                          MinicExpressionId left_expression_id,
                                          MinicExpressionId right_expression_id) {
    const MinicExpression *left;
    const MinicExpression *right;

    if (program == NULL) {
        return false;
    }
    left = minic_c0_program_expression(program, left_expression_id);
    right = minic_c0_program_expression(program, right_expression_id);
    if (left == NULL || right == NULL) {
        return false;
    }
    if (minic_type_pointer_equality_compatible(left->type, right->type)) {
        return true;
    }
    return (minic_type_is_pointer(left->type) &&
            minic_c0_expression_is_null_pointer_constant_v0(program, right_expression_id)) ||
           (minic_c0_expression_is_null_pointer_constant_v0(program, left_expression_id) &&
            minic_type_is_pointer(right->type));
}

const MinicLocal *minic_c0_program_local(const MinicC0Program *program, MinicLocalId local_id) {
    if (local_id >= program->local_count) {
        return NULL;
    }
    return &program->locals[local_id];
}

const MinicStatement *minic_c0_program_statement(const MinicC0Program *program,
                                                 MinicStatementId statement_id) {
    if (statement_id >= program->statement_count) {
        return NULL;
    }
    return &program->statements[statement_id];
}

const MinicBlock *minic_c0_program_block(const MinicC0Program *program, MinicBlockId block_id) {
    if (block_id >= program->block_count) {
        return NULL;
    }
    return &program->blocks[block_id];
}

const MinicFunction *minic_c0_program_function(const MinicC0Program *program,
                                               MinicFunctionId function_id) {
    if (function_id >= program->function_count) {
        return NULL;
    }
    return &program->functions[function_id];
}

const MinicRecord *minic_c0_program_record(const MinicC0Program *program, MinicRecordId record_id) {
    if (program == NULL || record_id >= program->record_count) {
        return NULL;
    }
    return &program->records[record_id];
}

const MinicRecordField *minic_c0_record_field(const MinicRecord *record, size_t field_index) {
    if (record == NULL || field_index >= record->field_count) {
        return NULL;
    }
    return &record->fields[field_index];
}

const MinicArrayType *minic_c0_program_array_type(const MinicC0Program *program,
                                                  MinicArrayTypeId array_type_id) {
    if (program == NULL || array_type_id >= program->array_type_count) {
        return NULL;
    }
    return &program->array_types[array_type_id];
}

const MinicFunctionType *minic_c0_program_function_type(const MinicC0Program *program,
                                                        MinicFunctionTypeId function_type_id) {
    if (program == NULL || function_type_id >= program->function_type_count) {
        return NULL;
    }
    return &program->function_types[function_type_id];
}

const MinicTypeAlias *minic_c0_program_type_alias(const MinicC0Program *program,
                                                  MinicTypeAliasId alias_id) {
    if (program == NULL || alias_id >= program->type_alias_count) {
        return NULL;
    }
    return &program->type_aliases[alias_id];
}

const MinicInlineAsm *minic_c0_program_inline_asm(const MinicC0Program *program,
                                                  MinicInlineAsmId inline_asm_id) {
    if (program == NULL || inline_asm_id >= program->inline_asm_count) {
        return NULL;
    }
    return &program->inline_asms[inline_asm_id];
}

bool minic_c0_fixed_parameter_abi_type(const MinicC0Program *program,
                                       MinicType parameter_type,
                                       MinicType *abi_type) {
    const MinicRecord *record;
    const MinicRecordField *first_field;

    if (program == NULL || abi_type == NULL) {
        return false;
    }
    *abi_type = parameter_type;
    if (!minic_type_is_record(parameter_type)) {
        return true;
    }
    record = minic_c0_program_record(program, parameter_type.record_id);
    if (record == NULL || !record->is_transparent_union) {
        return record != NULL;
    }
    if (!record->is_complete || !record->is_union || record->field_count == 0U) {
        return false;
    }
    first_field = minic_c0_record_field(record, 0U);
    if (first_field == NULL || first_field->is_array || first_field->is_bit_field ||
        !minic_type_is_pointer(first_field->type)) {
        return false;
    }
    *abi_type = first_field->type;
    return true;
}

bool minic_c0_fixed_call_argument_compatible(const MinicC0Program *program,
                                             MinicType parameter_type,
                                             MinicExpressionId argument_expression_id) {
    const MinicRecord *record;
    size_t field_index;

    if (program == NULL) {
        return false;
    }
    if (minic_c0_assignment_compatible(program, parameter_type, argument_expression_id)) {
        return true;
    }
    if (!minic_type_is_record(parameter_type)) {
        return false;
    }
    record = minic_c0_program_record(program, parameter_type.record_id);
    if (record == NULL || !record->is_complete || !record->is_union ||
        !record->is_transparent_union || record->field_count == 0U) {
        return false;
    }
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->is_array || field->is_bit_field ||
            !minic_type_is_pointer(field->type)) {
            return false;
        }
        if (minic_c0_assignment_compatible(program, field->type, argument_expression_id)) {
            return true;
        }
    }
    return false;
}
