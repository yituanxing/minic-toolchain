#include "target/riscv64/codegen.h"
#include "target/riscv64/codegen_internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char *const minic_riscv64_argument_registers[8] = {
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};

static bool minic_riscv64_alignment_power(size_t alignment, unsigned int *power) {
    unsigned int result;
    size_t value;

    if (alignment == 0U || power == NULL) {
        return false;
    }
    result = 0U;
    value = alignment;
    while (value > 1U) {
        if ((value & 1U) != 0U) {
            return false;
        }
        value >>= 1U;
        result += 1U;
    }
    *power = result;
    return true;
}

static bool minic_riscv64_global_scalar_type(const MinicC0Program *program,
                                             MinicType object_type,
                                             MinicType *scalar_type,
                                             size_t *scalar_width) {
    MinicType type;

    if (program == NULL || scalar_type == NULL || scalar_width == NULL) {
        return false;
    }
    type = object_type;
    while (minic_type_is_array(type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL) {
            return false;
        }
        type = array_type->element_type;
    }
    if (!minic_type_is_integer(type)) {
        return false;
    }
    *scalar_type = type;
    *scalar_width = minic_type_is_char_integer(type)   ? 1U
                    : minic_type_is_long_integer(type) ? 8U
                                                       : 4U;
    return true;
}

static bool minic_riscv64_emit_zero_bytes(FILE *file, size_t size) {
    return size == 0U || fprintf(file, "  .zero %zu\n", size) >= 0;
}

static bool
emit_fn_relocs(FILE *file, const MinicC0Program *program, const MinicGlobalObject *object) {
    const MinicRecord *record;
    MinicType pointee;
    size_t cursor;
    size_t relocation_index;

    if (file == NULL || program == NULL || object == NULL || !object->is_zero_initialized ||
        object->function_relocation_count == 0U || object->initializer_count != 0U) {
        return false;
    }
    if (minic_type_pointee(object->type, &pointee) && minic_type_is_function(pointee)) {
        const MinicGlobalFunctionRelocation *relocation;
        const MinicFunction *function;

        if (object->function_relocation_count != 1U || object->storage_size < 8U) {
            return false;
        }
        relocation = &object->function_relocations[0];
        function = minic_c0_program_function(program, relocation->function_id);
        if (function == NULL || function->name_length == 0U ||
            fprintf(file, "  .dword %s\n", function->name) < 0) {
            return false;
        }
        return minic_riscv64_emit_zero_bytes(file, object->storage_size - 8U);
    }
    if (!minic_type_is_record(object->type)) {
        return false;
    }
    record = minic_c0_program_record(program, object->type.record_id);
    if (record == NULL || !record->is_complete) {
        return false;
    }

    cursor = 0U;
    for (relocation_index = 0U; relocation_index < object->function_relocation_count;
         ++relocation_index) {
        const MinicGlobalFunctionRelocation *relocation;
        const MinicRecordField *field;
        const MinicFunction *function;
        MinicType pointee;
        size_t field_offset;

        relocation = &object->function_relocations[relocation_index];
        field = minic_c0_record_field(record, relocation->field_index);
        function = minic_c0_program_function(program, relocation->function_id);
        if (field == NULL || function == NULL || function->name_length == 0U ||
            field->element_count != 1U || !minic_type_pointee(field->type, &pointee) ||
            !minic_type_is_function(pointee)) {
            return false;
        }
        field_offset = field->storage_offset;
        if (field_offset < cursor || field_offset > object->storage_size ||
            object->storage_size - field_offset < 8U ||
            !minic_riscv64_emit_zero_bytes(file, field_offset - cursor) ||
            fprintf(file, "  .dword %s\n", function->name) < 0) {
            return false;
        }
        cursor = field_offset + 8U;
    }
    return cursor <= object->storage_size &&
           minic_riscv64_emit_zero_bytes(file, object->storage_size - cursor);
}

static bool minic_riscv64_emit_global_object(FILE *file,
                                             const MinicC0Program *program,
                                             const MinicGlobalObject *object) {
    MinicType scalar_type;
    const char *directive;
    unsigned int alignment_power;
    size_t scalar_width;
    size_t initializer_index;

    if (file == NULL || program == NULL || object == NULL || object->name_length == 0U ||
        object->storage_size == 0U || object->alignment == 0U ||
        !minic_riscv64_alignment_power(object->alignment, &alignment_power)) {
        return false;
    }

    directive = NULL;
    scalar_width = 0U;
    if (object->is_zero_initialized) {
        if (object->initializer_count != 0U) {
            return false;
        }
    } else {
        if (object->function_relocation_count != 0U ||
            !minic_riscv64_global_scalar_type(program, object->type, &scalar_type, &scalar_width) ||
            scalar_width == 0U || object->initializer_count > object->storage_size / scalar_width) {
            return false;
        }
        directive = minic_type_is_char_integer(scalar_type)   ? ".byte"
                    : minic_type_is_long_integer(scalar_type) ? ".dword"
                                                              : ".word";
    }

    if (fprintf(file, "%s\n", object->is_read_only ? ".section .rodata" : ".data") < 0) {
        return false;
    }
    if (!object->is_internal && fprintf(file, ".globl %s\n", object->name) < 0) {
        return false;
    }
    if (fprintf(file,
                ".type %s, @object\n"
                ".align %u\n"
                "%s:\n",
                object->name,
                alignment_power,
                object->name) < 0) {
        return false;
    }
    if (object->function_relocation_count != 0U) {
        if (!emit_fn_relocs(file, program, object)) {
            return false;
        }
    } else if (object->is_zero_initialized) {
        if (!minic_riscv64_emit_zero_bytes(file, object->storage_size)) {
            return false;
        }
    } else {
        for (initializer_index = 0U; initializer_index < object->initializer_count;
             ++initializer_index) {
            if (minic_type_is_char_integer(scalar_type)) {
                unsigned int value;

                value = (unsigned int)object->initializer_values[initializer_index] & 0xffU;
                if (fprintf(file, "  %s %u\n", directive, value) < 0) {
                    return false;
                }
            } else if (fprintf(file,
                               "  %s %d\n",
                               directive,
                               object->initializer_values[initializer_index]) < 0) {
                return false;
            }
        }
    }
    return fprintf(file, ".size %s, %zu\n", object->name, object->storage_size) >= 0;
}

static bool minic_riscv64_emit_function(FILE *file,
                                        const MinicC0Program *program,
                                        const MinicFunction *function,
                                        size_t *label_counter) {
    MinicRiscv64FrameLayout frame_layout;
    size_t frame_size;
    bool success;

    if (function == NULL || !function->is_defined || function->name_length == 0U ||
        function->body_block >= program->block_count ||
        !minic_riscv64_frame_layout(program, function, &frame_layout)) {
        return false;
    }
    frame_size = frame_layout.frame_size;

    success = true;
    if (!function->is_internal) {
        success = fprintf(file, ".globl %s\n", function->name) >= 0;
    }
    if (success) {
        success = fprintf(file,
                          ".type %s, @function\n"
                          "%s:\n",
                          function->name,
                          function->name) >= 0;
    }
    if (success) {
        success = minic_riscv64_emit_stack_allocate(file, frame_size);
    }
    if (success) {
        success = minic_riscv64_emit_sp_store64(file, "ra", frame_layout.saved_ra_offset) &&
                  minic_riscv64_emit_sp_store64(file, "s0", frame_layout.saved_s0_offset) &&
                  fprintf(file, "  mv s0, sp\n") >= 0;
    }
    if (success && function->is_variadic) {
        size_t register_index;

        for (register_index = frame_layout.integer_parameter_count; success && register_index < 8U;
             ++register_index) {
            size_t offset;

            offset = frame_layout.varargs_offset +
                     (register_index - frame_layout.integer_parameter_count) * 8U;
            success = minic_riscv64_emit_sp_store64(
                file, minic_riscv64_argument_registers[register_index], offset);
        }
    }
    if (success && function->parameter_count > 8U) {
        return false;
    }
    if (success) {
        size_t parameter_index;
        size_t integer_register_index;
        size_t floating_register_index;

        integer_register_index = 0U;
        floating_register_index = 0U;

        for (parameter_index = 0U; success && parameter_index < function->parameter_count;
             ++parameter_index) {
            const MinicLocal *parameter;
            MinicLocalId local_id;

            local_id = function->local_begin + parameter_index;
            parameter = minic_c0_program_local(program, local_id);
            if (parameter == NULL) {
                success = false;
                break;
            }
            if (minic_type_is_double(parameter->type) || minic_type_is_float(parameter->type)) {
                if (floating_register_index >= 8U) {
                    success = false;
                    break;
                }
                success = fprintf(file,
                                  minic_type_is_double(parameter->type) ? "  fmv.x.d t0, fa%zu\n"
                                                                        : "  fmv.x.w t0, fa%zu\n",
                                  floating_register_index) >= 0 &&
                          minic_riscv64_emit_object_store_register(
                              file, program, function, local_id, "t0");
                floating_register_index += 1U;
            } else {
                if (integer_register_index >= 8U) {
                    success = false;
                    break;
                }
                success = minic_riscv64_emit_object_store_register(
                    file,
                    program,
                    function,
                    local_id,
                    minic_riscv64_argument_registers[integer_register_index]);
                integer_register_index += 1U;
            }
        }
    }
    if (success) {
        success =
            minic_riscv64_emit_block(file, program, function, function->body_block, label_counter);
    }
    if (success) {
        success = fprintf(file,
                          "  li a0, 0\n"
                          ".L%s_return:\n",
                          function->name) >= 0;
    }
    if (success) {
        success = minic_riscv64_emit_sp_load64(file, "ra", frame_layout.saved_ra_offset) &&
                  minic_riscv64_emit_sp_load64(file, "s0", frame_layout.saved_s0_offset);
    }
    if (success) {
        success = minic_riscv64_emit_stack_release(file, frame_size);
    }
    if (success) {
        success = fprintf(file,
                          "  ret\n"
                          ".size %s, .-%s\n",
                          function->name,
                          function->name) >= 0;
    }
    return success;
}

bool minic_riscv64_write_c0_program(const char *path,
                                    const MinicC0Program *program,
                                    MinicDiagnostic *diagnostic) {
    FILE *file;
    size_t global_index;
    size_t function_index;
    size_t label_counter;
    bool success;

    if (program == NULL) {
        minic_riscv64_set_diagnostic(diagnostic, path, "program is required");
        return false;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        char message[256];

        (void)snprintf(message, sizeof(message), "cannot open output: %s", strerror(errno));
        minic_riscv64_set_diagnostic(diagnostic, path, message);
        return false;
    }

    success = true;
    for (global_index = 0U; success && global_index < program->global_object_count;
         ++global_index) {
        success =
            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);
        if (!success) {
            fprintf(stderr,
                    "CODEGEN_FAIL global=%zu name=%s\n",
                    global_index,
                    program->global_objects[global_index].name);
        }
    }
    if (success) {
        success = fprintf(file, ".text\n") >= 0;
    }

    label_counter = 0U;
    for (function_index = 0U; success && function_index < program->function_count;
         ++function_index) {
        const MinicFunction *function;

        function = &program->functions[function_index];
        if (!function->is_defined) {
            continue;
        }
        success = minic_riscv64_emit_function(file, program, function, &label_counter);
        if (!success) {
            fprintf(stderr,
                    "CODEGEN_FAIL function=%zu name=%s body=%zu\n",
                    function_index,
                    function->name,
                    (size_t)function->body_block);
            if (program->statement_count > 1482U) {
                const MinicStatement *failed_statement;
                const MinicExpression *target;
                const MinicExpression *value;
                const MinicExpression *member_base;
                const MinicLocal *target_local;
                const MinicLocal *base_local;

                failed_statement = &program->statements[1482U];
                target = minic_c0_program_expression(program, failed_statement->target_expression);
                value = minic_c0_program_expression(program, failed_statement->expression);
                member_base = value != NULL && value->kind == MINIC_EXPRESSION_MEMBER
                                  ? minic_c0_program_expression(program, value->value.member.base)
                                  : NULL;
                target_local = target != NULL && target->kind == MINIC_EXPRESSION_LOCAL
                                   ? minic_c0_program_local(program, target->value.local_id)
                                   : NULL;
                base_local = member_base != NULL && member_base->kind == MINIC_EXPRESSION_LOCAL
                                 ? minic_c0_program_local(program, member_base->value.local_id)
                                 : NULL;
                fprintf(
                    stderr,
                    "CODEGEN_DETAIL statement=1482 kind=%d target=%zu target_kind=%d "
                    "target_cat=%d target_int=%d target_ptr=%d target_float=%d target_double=%d "
                    "value=%zu value_kind=%d value_cat=%d value_int=%d value_ptr=%d "
                    "value_float=%d value_double=%d\n",
                    (int)failed_statement->kind,
                    (size_t)failed_statement->target_expression,
                    target != NULL ? (int)target->kind : -1,
                    target != NULL ? (int)target->value_category : -1,
                    target != NULL ? (int)minic_type_is_integer(target->type) : -1,
                    target != NULL ? (int)minic_type_is_pointer(target->type) : -1,
                    target != NULL ? (int)minic_type_is_float(target->type) : -1,
                    target != NULL ? (int)minic_type_is_double(target->type) : -1,
                    (size_t)failed_statement->expression,
                    value != NULL ? (int)value->kind : -1,
                    value != NULL ? (int)value->value_category : -1,
                    value != NULL ? (int)minic_type_is_integer(value->type) : -1,
                    value != NULL ? (int)minic_type_is_pointer(value->type) : -1,
                    value != NULL ? (int)minic_type_is_float(value->type) : -1,
                    value != NULL ? (int)minic_type_is_double(value->type) : -1);
                fprintf(stderr,
                        "CODEGEN_MEMBER field=%zu record=%zu base=%zu base_kind=%d base_cat=%d "
                        "base_local=%zu base_storage=%zu target_local=%zu target_storage=%zu "
                        "function_local_begin=%zu function_local_count=%zu function_storage=%zu\n",
                        value != NULL && value->kind == MINIC_EXPRESSION_MEMBER
                            ? value->value.member.field_index
                            : (size_t)-1,
                        value != NULL && value->kind == MINIC_EXPRESSION_MEMBER
                            ? (size_t)value->value.member.record_id
                            : (size_t)-1,
                        value != NULL && value->kind == MINIC_EXPRESSION_MEMBER
                            ? (size_t)value->value.member.base
                            : (size_t)-1,
                        member_base != NULL ? (int)member_base->kind : -1,
                        member_base != NULL ? (int)member_base->value_category : -1,
                        member_base != NULL && member_base->kind == MINIC_EXPRESSION_LOCAL
                            ? (size_t)member_base->value.local_id
                            : (size_t)-1,
                        base_local != NULL ? base_local->storage_offset : (size_t)-1,
                        target != NULL && target->kind == MINIC_EXPRESSION_LOCAL
                            ? (size_t)target->value.local_id
                            : (size_t)-1,
                        target_local != NULL ? target_local->storage_offset : (size_t)-1,
                        function->local_begin,
                        function->local_count,
                        function->local_storage_size);
            }
        }
    }

    if (!success) {
        minic_riscv64_set_diagnostic(diagnostic, path, "cannot write RISC-V assembly");
    }
    if (fclose(file) != 0 && success) {
        minic_riscv64_set_diagnostic(diagnostic, path, "cannot close RISC-V assembly output");
        success = false;
    }
    return success;
}
