#include "target/riscv64/codegen.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/abi.h"
#include "target/data_layout.h"

#include <errno.h>
#include <inttypes.h>
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

static bool
minic_riscv64_integer_storage_width(const MinicC0Program *program, MinicType type, size_t *width) {
    size_t alignment;
    size_t size;

    if (program == NULL || width == NULL || !minic_type_is_integer(type) ||
        !minic_riscv64_type_layout(program, type, &size, &alignment) ||
        (size != 1U && size != 2U && size != 4U && size != 8U)) {
        return false;
    }
    (void)alignment;
    *width = size;
    return true;
}

static const char *minic_riscv64_integer_data_directive(size_t width) {
    return width == 1U   ? ".byte"
           : width == 2U ? ".half"
           : width == 4U ? ".word"
           : width == 8U ? ".dword"
                         : NULL;
}

static bool minic_riscv64_emit_typed_bits(FILE *file,
                                          const MinicC0Program *program,
                                          MinicType type,
                                          uint64_t bits) {
    const char *directive;
    size_t width;
    size_t alignment;
    uint64_t mask;

    if (file == NULL || program == NULL ||
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type)) ||
        !minic_riscv64_type_layout(program, type, &width, &alignment) ||
        (width != 1U && width != 2U && width != 4U && width != 8U)) {
        return false;
    }
    (void)alignment;
    directive = minic_riscv64_integer_data_directive(width);
    if (directive == NULL) {
        return false;
    }
    if (width < 8U) {
        const unsigned int bit_width = (unsigned int)(width * 8U);

        mask = (UINT64_C(1) << bit_width) - UINT64_C(1);
        bits &= mask;
    }
    /* Preserve the historical byte spelling as an unsigned payload. GNU as
     * consumes the same low 8 bits, and plain char on this target is unsigned. */
    if (width == 1U || (minic_type_is_integer(type) && minic_type_is_unsigned_integer(type))) {
        return fprintf(file, "  %s %" PRIu64 "\n", directive, bits) >= 0;
    }
    {
        int64_t signed_value;

        if (width < 8U) {
            const unsigned int bit_width = (unsigned int)(width * 8U);
            const uint64_t sign_bit = UINT64_C(1) << (bit_width - 1U);

            if ((bits & sign_bit) != 0U) {
                bits |= ~((UINT64_C(1) << bit_width) - UINT64_C(1));
            }
        }
        (void)memcpy(&signed_value, &bits, sizeof(signed_value));
        return fprintf(file, "  %s %" PRId64 "\n", directive, signed_value) >= 0;
    }
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
    if (!minic_riscv64_integer_storage_width(program, type, scalar_width)) {
        return false;
    }
    *scalar_type = type;
    return true;
}

static bool minic_riscv64_emit_zero_bytes(FILE *file, size_t size) {
    return size == 0U || fprintf(file, "  .zero %zu\n", size) >= 0;
}

static const char *
minic_riscv64_global_relocation_target_name(const MinicC0Program *program,
                                            const MinicGlobalRelocation *relocation) {
    if (program == NULL || relocation == NULL) {
        return NULL;
    }
    if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT) {
        const MinicGlobalObject *target;

        target = minic_c0_program_global_object(program, relocation->target_id);
        return target != NULL && target->name_length != 0U ? target->name : NULL;
    }
    if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
        const MinicFunction *target;

        target = minic_c0_program_function(program, relocation->target_id);
        return target != NULL && target->name_length != 0U ? minic_c0_function_symbol_name(target)
                                                           : NULL;
    }
    return NULL;
}

static bool minic_riscv64_emit_symbol_value(FILE *file,
                                            const MinicC0Program *program,
                                            const MinicGlobalRelocation *relocation,
                                            size_t width) {
    const char *directive;
    const char *target_name;
    size_t target_addend;

    directive = minic_riscv64_integer_data_directive(width);
    target_name = minic_riscv64_global_relocation_target_name(program, relocation);
    if (file == NULL || directive == NULL || target_name == NULL || target_name[0] == '\0' ||
        !minic_data_layout_global_relocation_target_addend(
            minic_default_data_layout(), program, relocation, &target_addend)) {
        return false;
    }
    if (target_addend == 0U) {
        return fprintf(file, "  %s %s\n", directive, target_name) >= 0;
    }
    return fprintf(file, "  %s %s+%zu\n", directive, target_name, target_addend) >= 0;
}

static bool
emit_symbol_relocs(FILE *file, const MinicC0Program *program, const MinicGlobalObject *object) {
    MinicType pointer_type;
    size_t pointer_width;
    size_t pointer_alignment;
    size_t cursor;
    size_t relocation_index;

    if (file == NULL || program == NULL || object == NULL || !object->is_zero_initialized ||
        object->relocation_count == 0U || object->initializer_count != 0U ||
        !minic_type_pointer_to(minic_type_void(), &pointer_type) ||
        !minic_riscv64_type_layout(program, pointer_type, &pointer_width, &pointer_alignment) ||
        pointer_width != 8U) {
        return false;
    }
    (void)pointer_alignment;

    cursor = 0U;
    for (relocation_index = 0U; relocation_index < object->relocation_count; ++relocation_index) {
        const MinicGlobalRelocation *relocation;
        size_t storage_offset;

        relocation = &object->relocations[relocation_index];
        if (!minic_data_layout_global_relocation_offset(
                minic_default_data_layout(), program, object, relocation, &storage_offset)) {
            return false;
        }
        if (storage_offset < cursor || storage_offset > object->storage_size ||
            pointer_width > object->storage_size - storage_offset ||
            !minic_riscv64_emit_zero_bytes(file, storage_offset - cursor) ||
            !minic_riscv64_emit_symbol_value(file, program, relocation, pointer_width)) {
            return false;
        }
        cursor = storage_offset + pointer_width;
    }
    return cursor <= object->storage_size &&
           minic_riscv64_emit_zero_bytes(file, object->storage_size - cursor);
}

static bool minic_riscv64_emit_direct_record_values(FILE *file,
                                                    const MinicC0Program *program,
                                                    const MinicGlobalObject *object,
                                                    const MinicRecord *record) {
    size_t cursor;
    size_t field_index;
    size_t relocation_index;

    if (file == NULL || program == NULL || object == NULL || record == NULL ||
        !record->is_complete || record->is_union ||
        object->initializer_count != record->field_count) {
        return false;
    }
    cursor = 0U;
    relocation_index = 0U;
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;
        const MinicGlobalRelocation *relocation;
        size_t field_size;
        size_t field_alignment;
        size_t field_offset;
        uint64_t value;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_flexible_array ||
            !minic_riscv64_type_layout(program, field->type, &field_size, &field_alignment)) {
            return false;
        }
        (void)field_alignment;
        field_offset = field->storage_offset;
        if (field_offset < cursor || field_offset > object->storage_size ||
            field_size > object->storage_size - field_offset ||
            !minic_riscv64_emit_zero_bytes(file, field_offset - cursor)) {
            return false;
        }
        value = object->initializer_values[field_index];
        relocation = relocation_index < object->relocation_count
                         ? &object->relocations[relocation_index]
                         : NULL;
        if (relocation != NULL &&
            relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD &&
            relocation->location_index == field_index) {
            if (!minic_type_is_pointer(field->type) || value != 0U ||
                !minic_riscv64_emit_symbol_value(file, program, relocation, field_size)) {
                return false;
            }
            relocation_index += 1U;
        } else if (minic_type_is_integer(field->type) || minic_type_is_pointer(field->type)) {
            if (!minic_riscv64_emit_typed_bits(file, program, field->type, value)) {
                return false;
            }
        } else {
            if (value != 0U || !minic_type_is_record(field->type) ||
                !minic_riscv64_emit_zero_bytes(file, field_size)) {
                return false;
            }
        }
        cursor = field_offset + field_size;
    }
    return relocation_index == object->relocation_count && cursor <= object->storage_size &&
           minic_riscv64_emit_zero_bytes(file, object->storage_size - cursor);
}

static bool minic_riscv64_emit_constant_value(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicGlobalObject *object,
                                              MinicType type,
                                              size_t *initializer_index,
                                              size_t *relocation_index,
                                              size_t *emitted_size) {
    size_t type_size;
    size_t type_alignment;

    if (file == NULL || program == NULL || object == NULL || initializer_index == NULL ||
        relocation_index == NULL || emitted_size == NULL ||
        !minic_riscv64_type_layout(program, type, &type_size, &type_alignment)) {
        return false;
    }
    (void)type_alignment;
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        const MinicGlobalRelocation *relocation;
        uint64_t bits;
        size_t slot_index;

        if (*initializer_index >= object->initializer_count) {
            return false;
        }
        slot_index = *initializer_index;
        bits = object->initializer_values[slot_index];
        *initializer_index += 1U;
        relocation = *relocation_index < object->relocation_count
                         ? &object->relocations[*relocation_index]
                         : NULL;
        if (relocation != NULL &&
            relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
            relocation->location_index == slot_index) {
            if (!minic_type_is_pointer(type) || bits != 0U ||
                !minic_riscv64_emit_symbol_value(file, program, relocation, type_size)) {
                return false;
            }
            *relocation_index += 1U;
        } else if (!minic_riscv64_emit_typed_bits(file, program, type, bits)) {
            return false;
        }
        *emitted_size = type_size;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t cursor;
        size_t element_index;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U) {
            return false;
        }
        cursor = 0U;
        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
            size_t element_emitted;

            if (!minic_riscv64_emit_constant_value(file,
                                                   program,
                                                   object,
                                                   array_type->element_type,
                                                   initializer_index,
                                                   relocation_index,
                                                   &element_emitted) ||
                cursor > type_size - element_emitted) {
                return false;
            }
            cursor += element_emitted;
        }
        if (cursor > type_size || !minic_riscv64_emit_zero_bytes(file, type_size - cursor)) {
            return false;
        }
        *emitted_size = type_size;
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t cursor;
        size_t field_index;
        size_t field_limit;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete || record->field_count == 0U) {
            return false;
        }
        cursor = 0U;
        field_limit = record->is_union ? 1U : record->field_count;
        for (field_index = 0U; field_index < field_limit; ++field_index) {
            const MinicRecordField *field;
            size_t element_index;
            size_t field_offset;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U || field->is_flexible_array) {
                return false;
            }
            field_offset = record->is_union ? 0U : field->storage_offset;
            if (field_offset < cursor || field_offset > type_size ||
                !minic_riscv64_emit_zero_bytes(file, field_offset - cursor)) {
                return false;
            }
            cursor = field_offset;
            for (element_index = 0U; element_index < field->element_count; ++element_index) {
                size_t element_emitted;

                if (!minic_riscv64_emit_constant_value(file,
                                                       program,
                                                       object,
                                                       field->type,
                                                       initializer_index,
                                                       relocation_index,
                                                       &element_emitted) ||
                    cursor > type_size - element_emitted) {
                    return false;
                }
                cursor += element_emitted;
            }
            if (record->is_union) {
                break;
            }
        }
        if (cursor > type_size || !minic_riscv64_emit_zero_bytes(file, type_size - cursor)) {
            return false;
        }
        *emitted_size = type_size;
        return true;
    }
    return false;
}

static bool minic_riscv64_emit_record_values(FILE *file,
                                             const MinicC0Program *program,
                                             const MinicGlobalObject *object) {
    const MinicRecord *record;
    size_t emitted_size;
    size_t initializer_index;
    size_t relocation_index;

    if (file == NULL || program == NULL || object == NULL || !minic_type_is_record(object->type) ||
        object->is_zero_initialized) {
        return false;
    }
    record = minic_c0_program_record(program, object->type.record_id);
    if (record == NULL || !record->is_complete) {
        return false;
    }
    {
        bool has_recursive_relocation;
        size_t index;

        has_recursive_relocation = false;
        for (index = 0U; index < object->relocation_count; ++index) {
            if (object->relocations[index].location_kind ==
                MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) {
                has_recursive_relocation = true;
                break;
            }
        }
        if (!record->is_union && object->initializer_count == record->field_count &&
            !has_recursive_relocation) {
            return minic_riscv64_emit_direct_record_values(file, program, object, record);
        }
    }
    initializer_index = 0U;
    relocation_index = 0U;
    emitted_size = 0U;
    return minic_riscv64_emit_constant_value(file,
                                             program,
                                             object,
                                             object->type,
                                             &initializer_index,
                                             &relocation_index,
                                             &emitted_size) &&
           initializer_index == object->initializer_count &&
           relocation_index == object->relocation_count && emitted_size == object->storage_size;
}

static bool minic_riscv64_record_array_info(const MinicC0Program *program,
                                            MinicType type,
                                            const MinicArrayType **array_type_out,
                                            const MinicRecord **record_out) {
    const MinicArrayType *array_type;
    const MinicRecord *record;

    if (program == NULL || !minic_type_is_array(type)) {
        return false;
    }
    array_type = minic_c0_program_array_type(program, type.array_type_id);
    if (array_type == NULL || !minic_type_is_record(array_type->element_type)) {
        return false;
    }
    record = minic_c0_program_record(program, array_type->element_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union) {
        return false;
    }
    if (array_type_out != NULL) {
        *array_type_out = array_type;
    }
    if (record_out != NULL) {
        *record_out = record;
    }
    return true;
}

static bool minic_riscv64_emit_record_array_values(FILE *file,
                                                   const MinicC0Program *program,
                                                   const MinicGlobalObject *object) {
    const MinicArrayType *array_type;
    const MinicRecord *record;
    size_t element_size;
    size_t element_alignment;
    size_t cursor;
    size_t element_index;
    size_t initializer_index;

    if (file == NULL || program == NULL || object == NULL || object->is_zero_initialized ||
        object->relocation_count != 0U ||
        !minic_riscv64_record_array_info(program, object->type, &array_type, &record) ||
        record->field_count == 0U || array_type->element_count > SIZE_MAX / record->field_count ||
        object->initializer_count != array_type->element_count * record->field_count ||
        !minic_riscv64_type_layout(
            program, array_type->element_type, &element_size, &element_alignment) ||
        element_size == 0U || array_type->element_count > SIZE_MAX / element_size ||
        object->storage_size != array_type->element_count * element_size) {
        return false;
    }
    (void)element_alignment;

    cursor = 0U;
    initializer_index = 0U;
    for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
        size_t field_index;
        size_t element_base;

        element_base = element_index * element_size;
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *field;
            const char *directive;
            size_t field_size;
            size_t field_alignment;
            size_t field_offset;
            uint64_t value;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count != 1U || field->is_flexible_array ||
                !minic_type_is_integer(field->type) ||
                !minic_riscv64_type_layout(program, field->type, &field_size, &field_alignment)) {
                return false;
            }
            (void)field_alignment;
            if (field->storage_offset > element_size ||
                field_size > element_size - field->storage_offset) {
                return false;
            }
            field_offset = element_base + field->storage_offset;
            if (field_offset < cursor || field_offset > object->storage_size ||
                field_size > object->storage_size - field_offset ||
                !minic_riscv64_emit_zero_bytes(file, field_offset - cursor)) {
                return false;
            }

            value = object->initializer_values[initializer_index++];
            directive = minic_riscv64_integer_data_directive(field_size);
            if (directive == NULL ||
                !minic_riscv64_emit_typed_bits(file, program, field->type, value)) {
                return false;
            }
            cursor = field_offset + field_size;
        }
        if (cursor > element_base + element_size ||
            !minic_riscv64_emit_zero_bytes(file, element_base + element_size - cursor)) {
            return false;
        }
        cursor = element_base + element_size;
    }
    return initializer_index == object->initializer_count && cursor == object->storage_size;
}

static bool minic_riscv64_emit_file_asm(FILE *file, const MinicFileAsm *file_asm) {
    if (file == NULL || file_asm == NULL || file_asm->text == NULL) {
        return false;
    }
    if (file_asm->length != 0U &&
        fwrite(file_asm->text, 1U, file_asm->length, file) != file_asm->length) {
        return false;
    }
    return fputc('\n', file) != EOF;
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
    if (object->is_zero_initialized || object->is_tentative) {
        if (object->initializer_count != 0U) {
            return false;
        }
    } else if (minic_type_is_record(object->type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete || object->initializer_count == 0U) {
            return false;
        }
    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL)) {
        const MinicArrayType *array_type;
        const MinicRecord *record;

        if (!minic_riscv64_record_array_info(program, object->type, &array_type, &record) ||
            record->field_count == 0U ||
            array_type->element_count > SIZE_MAX / record->field_count ||
            object->relocation_count != 0U ||
            object->initializer_count != array_type->element_count * record->field_count) {
            return false;
        }
    } else {
        if (object->relocation_count != 0U ||
            !minic_riscv64_global_scalar_type(program, object->type, &scalar_type, &scalar_width) ||
            scalar_width == 0U || object->initializer_count > object->storage_size / scalar_width) {
            return false;
        }
        directive = minic_riscv64_integer_data_directive(scalar_width);
        if (directive == NULL) {
            return false;
        }
    }

    if (object->section_name != NULL) {
        if (fprintf(file, ".section %s\n", object->section_name) < 0) {
            return false;
        }
    } else if (fprintf(file, "%s\n", object->is_read_only ? ".section .rodata" : ".data") < 0) {
        return false;
    }
    if (!object->is_internal) {
        if (fprintf(file, ".globl %s\n", object->name) < 0) {
            return false;
        }
        if (object->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT) {
            const char *visibility_directive;

            visibility_directive =
                object->visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN      ? ".hidden"
                : object->visibility == MINIC_SYMBOL_VISIBILITY_INTERNAL  ? ".internal"
                : object->visibility == MINIC_SYMBOL_VISIBILITY_PROTECTED ? ".protected"
                                                                          : NULL;
            if (visibility_directive == NULL ||
                fprintf(file, "%s %s\n", visibility_directive, object->name) < 0) {
                return false;
            }
        }
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
    if (minic_type_is_record(object->type) && object->initializer_count != 0U) {
        if (!minic_riscv64_emit_record_values(file, program, object)) {
            return false;
        }
    } else if (object->relocation_count != 0U) {
        if (!emit_symbol_relocs(file, program, object)) {
            return false;
        }
    } else if (object->is_zero_initialized || object->is_tentative) {
        if (!minic_riscv64_emit_zero_bytes(file, object->storage_size)) {
            return false;
        }
    } else if (minic_type_is_record(object->type)) {
        if (!minic_riscv64_emit_record_values(file, program, object)) {
            return false;
        }
    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL)) {
        if (!minic_riscv64_emit_record_array_values(file, program, object)) {
            return false;
        }
    } else {
        for (initializer_index = 0U; initializer_index < object->initializer_count;
             ++initializer_index) {
            if (!minic_riscv64_emit_typed_bits(
                    file, program, scalar_type, object->initializer_values[initializer_index])) {
                return false;
            }
        }
        if (!minic_riscv64_emit_zero_bytes(
                file, object->storage_size - object->initializer_count * scalar_width)) {
            return false;
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
    const char *symbol_name;

    if (function == NULL || !function->is_defined || function->name_length == 0U ||
        function->body_block >= program->block_count ||
        !minic_riscv64_frame_layout(program, function, &frame_layout)) {
        return false;
    }
    frame_size = frame_layout.frame_size;
    symbol_name = minic_c0_function_symbol_name(function);
    if (symbol_name == NULL || symbol_name[0] == '\0') {
        return false;
    }

    success = function->section_name != NULL
                  ? fprintf(file, ".section %s\n", function->section_name) >= 0
                  : fprintf(file, ".text\n") >= 0;
    if (success && !function->is_internal) {
        success = fprintf(file, function->is_weak ? ".weak %s\n" : ".globl %s\n", symbol_name) >= 0;
        if (success && function->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT) {
            const char *directive;

            directive = function->visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN      ? ".hidden"
                        : function->visibility == MINIC_SYMBOL_VISIBILITY_INTERNAL  ? ".internal"
                        : function->visibility == MINIC_SYMBOL_VISIBILITY_PROTECTED ? ".protected"
                                                                                    : NULL;
            success = directive != NULL && fprintf(file, "%s %s\n", directive, symbol_name) >= 0;
        }
    }
    if (success) {
        success = fprintf(file,
                          ".type %s, @function\n"
                          "%s:\n",
                          symbol_name,
                          symbol_name) >= 0;
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
    if (success) {
        MinicRiscv64AbiCursor abi_cursor;
        size_t parameter_index;

        minic_riscv64_abi_cursor_initialize(&abi_cursor);
        for (parameter_index = 0U; success && parameter_index < function->parameter_count;
             ++parameter_index) {
            const MinicLocal *parameter;
            MinicLocalId local_id;
            MinicRiscv64AbiArgumentLocation location;

            local_id = function->local_begin + parameter_index;
            parameter = minic_c0_program_local(program, local_id);
            if (parameter == NULL || !minic_riscv64_abi_place_argument(
                                         program, parameter->type, true, &abi_cursor, &location)) {
                success = false;
                break;
            }

            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT) {
                if (location.floating_register_count != 1U ||
                    location.floating_register_begin >= 8U ||
                    location.integer_register_count != 0U || location.stack_slot_count != 0U) {
                    success = false;
                    break;
                }
                success = fprintf(file,
                                  minic_type_is_double(parameter->type) ? "  fmv.x.d t0, fa%zu\n"
                                                                        : "  fmv.x.w t0, fa%zu\n",
                                  location.floating_register_begin) >= 0 &&
                          minic_riscv64_emit_object_store_register(
                              file, program, function, local_id, "t0");
                continue;
            }

            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                size_t chunk_index;

                if (location.value.slot_count == 0U ||
                    location.value.slot_count !=
                        location.integer_register_count + location.stack_slot_count ||
                    location.integer_register_begin > 8U ||
                    location.integer_register_count > 8U - location.integer_register_begin) {
                    success = false;
                    break;
                }
                for (chunk_index = 0U; success && chunk_index < location.value.slot_count;
                     ++chunk_index) {
                    const char *source_register;

                    source_register = "t0";
                    if (chunk_index < location.integer_register_count) {
                        source_register =
                            minic_riscv64_argument_registers[location.integer_register_begin +
                                                             chunk_index];
                    } else {
                        size_t incoming_offset;
                        size_t stack_slot;

                        stack_slot = location.stack_slot_begin +
                                     (chunk_index - location.integer_register_count);
                        if (stack_slot > (SIZE_MAX - frame_size) / 8U) {
                            success = false;
                            break;
                        }
                        incoming_offset = frame_size + stack_slot * 8U;
                        success = minic_riscv64_emit_sp_load64(file, "t0", incoming_offset);
                    }
                    if (success) {
                        success = minic_riscv64_emit_integer_aggregate_local_chunk(
                            file, program, function, local_id, chunk_index, source_register);
                    }
                }
                continue;
            }

            if (location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
                location.floating_register_count != 0U) {
                success = false;
                break;
            }
            if (location.integer_register_count == 1U && location.stack_slot_count == 0U &&
                location.integer_register_begin < 8U) {
                success = minic_riscv64_emit_object_store_register(
                    file,
                    program,
                    function,
                    local_id,
                    minic_riscv64_argument_registers[location.integer_register_begin]);
                continue;
            }
            if (location.integer_register_count == 0U && location.stack_slot_count == 1U) {
                size_t incoming_offset;

                if (location.stack_slot_begin > (SIZE_MAX - frame_size) / 8U) {
                    success = false;
                    break;
                }
                incoming_offset = frame_size + location.stack_slot_begin * 8U;
                success = minic_riscv64_emit_sp_load64(file, "t0", incoming_offset) &&
                          minic_riscv64_emit_object_store_register(
                              file, program, function, local_id, "t0");
                continue;
            }
            success = false;
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
                          symbol_name,
                          symbol_name) >= 0;
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
        if (program->global_objects[global_index].is_extern) {
            continue;
        }
        success =
            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);
        if (!success) {
            fprintf(stderr,
                    "CODEGEN_FAIL global=%zu name=%s\n",
                    global_index,
                    program->global_objects[global_index].name);
        }
    }
    if (success && program->file_asm_count != 0U) {
        size_t file_asm_index;

        success = fprintf(file, ".text\n") >= 0;
        for (file_asm_index = 0U; success && file_asm_index < program->file_asm_count;
             ++file_asm_index) {
            success = minic_riscv64_emit_file_asm(file, &program->file_asms[file_asm_index]);
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
            const char *symbol_name;

            symbol_name = minic_c0_function_symbol_name(function);
            if (function->is_weak && !function->is_internal) {
                success = symbol_name != NULL && symbol_name[0] != '\0' &&
                          fprintf(file, ".weak %s\n", symbol_name) >= 0;
            }
            continue;
        }
        success = minic_riscv64_emit_function(file, program, function, &label_counter);
        if (!success) {
            fprintf(stderr,
                    "CODEGEN_FAIL function=%zu name=%s body=%zu\n",
                    function_index,
                    function->name,
                    (size_t)function->body_block);
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
