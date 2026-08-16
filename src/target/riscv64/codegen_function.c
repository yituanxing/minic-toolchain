#include "target/riscv64/codegen_internal.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool minic_riscv64_write_section_name(FILE *file, const MinicSourceSpan *span) {
    size_t index;

    if (file == NULL || span == NULL || span->source == NULL || span->length == 0U) {
        return false;
    }
    for (index = 0U; index < span->length; ++index) {
        const unsigned char c = (unsigned char)span->source[index];

        if (c == '\n' || c == '\r' || c == '\0') {
            return false;
        }
    }
    return fwrite(span->source, 1U, span->length, file) == span->length;
}

static bool minic_riscv64_emit_symbol_visibility(FILE *file,
                                                  const char *symbol_name,
                                                  MinicSymbolVisibility visibility) {
    const char *directive;

    if (visibility == MINIC_SYMBOL_VISIBILITY_DEFAULT) {
        return true;
    }
    directive = visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN      ? ".hidden"
                : visibility == MINIC_SYMBOL_VISIBILITY_INTERNAL  ? ".internal"
                : visibility == MINIC_SYMBOL_VISIBILITY_PROTECTED ? ".protected"
                                                                   : NULL;
    return directive != NULL && fprintf(file, "%s %s\n", directive, symbol_name) >= 0;
}

static bool minic_riscv64_write_escaped_asm_string(FILE *file,
                                                   const unsigned char *bytes,
                                                   size_t length) {
    size_t index;

    if (fputc('"', file) == EOF) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        const unsigned char value = bytes[index];

        if (value == '\\' || value == '"') {
            if (fputc('\\', file) == EOF || fputc((int)value, file) == EOF) {
                return false;
            }
        } else if (value >= 32U && value <= 126U) {
            if (fputc((int)value, file) == EOF) {
                return false;
            }
        } else if (fprintf(file, "\\%03o", (unsigned int)value) < 0) {
            return false;
        }
    }
    return fputc('"', file) != EOF;
}

static bool minic_riscv64_emit_string_literals(FILE *file, const MinicC0Program *program) {
    size_t index;

    for (index = 0U; index < program->string_literal_count; ++index) {
        const MinicStringLiteral *literal = &program->string_literals[index];

        if (fprintf(file,
                    ".section .rodata\n"
                    ".align 0\n"
                    ".Lminic_string_%zu:\n"
                    "  .asciz ",
                    index) < 0 ||
            !minic_riscv64_write_escaped_asm_string(file, literal->bytes, literal->length) ||
            fputc('\n', file) == EOF) {
            return false;
        }
    }
    return true;
}

static bool minic_riscv64_emit_compound_literal_data(FILE *file,
                                                     const MinicC0Program *program) {
    size_t index;

    for (index = 0U; index < program->compound_literal_count; ++index) {
        const MinicCompoundLiteral *literal = &program->compound_literals[index];
        const MinicRecord *record;
        size_t storage_size;
        size_t object_alignment;
        unsigned int alignment_power;
        MinicGlobalObject object;

        if (!minic_type_is_record(literal->type)) {
            continue;
        }
        record = minic_c0_program_record(program, literal->type.record_id);
        if (record == NULL || !record->is_complete || record->is_union ||
            !minic_data_layout_record(
                minic_default_data_layout(), program, record, &storage_size, &object_alignment) ||
            object_alignment == 0U ||
            !minic_riscv64_alignment_power(object_alignment, &alignment_power)) {
            return false;
        }
        (void)memset(&object, 0, sizeof(object));
        object.type = literal->type;
        object.initializer_values = literal->initializer_values;
        object.initializer_count = literal->initializer_count;
        object.relocations = literal->relocations;
        object.relocation_count = literal->relocation_count;
        object.is_read_only = false;
        if (fprintf(file,
                    ".data\n"
                    ".align %u\n"
                    ".Lminic_compound_literal_%zu:\n",
                    alignment_power,
                    index) < 0 ||
            !minic_riscv64_emit_record_values(file, program, &object)) {
            return false;
        }
    }
    return true;
}

static bool minic_riscv64_emit_zero_bytes(FILE *file, size_t byte_count) {
    return byte_count == 0U || fprintf(file, "  .zero %zu\n", byte_count) >= 0;
}

static bool minic_riscv64_emit_typed_bits(FILE *file,
                                          const MinicC0Program *program,
                                          MinicType type,
                                          uint64_t bits) {
    size_t type_size;
    size_t type_alignment;
    const char *directive;

    if (!minic_riscv64_type_layout(program, type, &type_size, &type_alignment)) {
        return false;
    }
    (void)type_alignment;
    directive = minic_riscv64_integer_data_directive(type_size);
    if (directive == NULL) {
        return false;
    }
    return fprintf(file, "  %s %" PRIu64 "\n", directive, bits) >= 0;
}

static bool minic_riscv64_emit_symbol_value(FILE *file,
                                            const MinicC0Program *program,
                                            const MinicGlobalRelocation *relocation,
                                            size_t storage_size) {
    const char *directive;
    const char *symbol_name;
    char local_name[96];

    if (relocation == NULL) {
        return false;
    }
    directive = minic_riscv64_integer_data_directive(storage_size);
    if (directive == NULL) {
        return false;
    }
    symbol_name = NULL;
    switch (relocation->symbol_kind) {
        case MINIC_GLOBAL_RELOCATION_SYMBOL_GLOBAL:
            if (relocation->symbol_index >= program->global_object_count) {
                return false;
            }
            symbol_name = program->global_objects[relocation->symbol_index].name;
            break;
        case MINIC_GLOBAL_RELOCATION_SYMBOL_FUNCTION:
            if (relocation->symbol_index >= program->function_count) {
                return false;
            }
            symbol_name = minic_c0_function_symbol_name(&program->functions[relocation->symbol_index]);
            break;
        case MINIC_GLOBAL_RELOCATION_SYMBOL_STRING:
            if (relocation->symbol_index >= program->string_literal_count ||
                snprintf(local_name,
                         sizeof(local_name),
                         ".Lminic_string_%u",
                         relocation->symbol_index) < 0) {
                return false;
            }
            symbol_name = local_name;
            break;
        case MINIC_GLOBAL_RELOCATION_SYMBOL_COMPOUND_LITERAL:
            if (relocation->symbol_index >= program->compound_literal_count ||
                snprintf(local_name,
                         sizeof(local_name),
                         ".Lminic_compound_literal_%u",
                         relocation->symbol_index) < 0) {
                return false;
            }
            symbol_name = local_name;
            break;
        default:
            return false;
    }
    if (symbol_name == NULL || symbol_name[0] == '\0') {
        return false;
    }
    if (relocation->addend == 0) {
        return fprintf(file, "  %s %s\n", directive, symbol_name) >= 0;
    }
    return fprintf(file,
                   "  %s %s%+" PRId64 "\n",
                   directive,
                   symbol_name,
                   relocation->addend) >= 0;
}

static bool minic_riscv64_emit_record_bit_field_run(FILE *file,
                                                     const MinicC0Program *program,
                                                     const MinicGlobalObject *object,
                                                     const MinicRecord *record,
                                                     size_t field_limit,
                                                     size_t record_size,
                                                     size_t *field_index,
                                                     size_t *initializer_index,
                                                     size_t *relocation_index,
                                                     size_t *cursor) {
    const MinicRecordField *first;
    size_t unit_offset;
    size_t unit_size;
    size_t unit_alignment;
    size_t scan;
    uint64_t unit_bits;

    if (file == NULL || program == NULL || object == NULL || record == NULL ||
        field_index == NULL || initializer_index == NULL || relocation_index == NULL ||
        cursor == NULL || *field_index >= field_limit) {
        return false;
    }
    first = minic_c0_record_field(record, *field_index);
    if (first == NULL || !first->is_bit_field || first->is_unnamed_bit_field ||
        !minic_data_layout_record_field_offset(
            minic_default_data_layout(), program, record, *field_index, &unit_offset) ||
        !minic_riscv64_type_layout(program, first->type, &unit_size, &unit_alignment)) {
        return false;
    }
    (void)unit_alignment;
    if (unit_offset < *cursor || unit_offset > record_size || unit_size > record_size - unit_offset ||
        !minic_riscv64_emit_zero_bytes(file, unit_offset - *cursor)) {
        return false;
    }
    unit_bits = 0U;
    scan = *field_index;
    while (scan < field_limit) {
        const MinicRecordField *field;
        size_t field_offset;
        size_t field_size;
        size_t field_alignment;
        uint64_t value;
        uint64_t mask;

        field = minic_c0_record_field(record, scan);
        if (field == NULL || !field->is_bit_field ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), program, record, scan, &field_offset) ||
            field_offset != unit_offset || !minic_riscv64_type_layout(program,
                                                                     field->type,
                                                                     &field_size,
                                                                     &field_alignment) ||
            field_size != unit_size) {
            break;
        }
        (void)field_alignment;
        if (field->is_unnamed_bit_field) {
            scan += 1U;
            continue;
        }
        if (*initializer_index >= object->initializer_count ||
            *relocation_index < object->relocation_count &&
                object->relocations[*relocation_index].location_kind ==
                    MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
                object->relocations[*relocation_index].location_index == *initializer_index) {
            return false;
        }
        value = object->initializer_values[*initializer_index];
        *initializer_index += 1U;
        if (field->bit_width == 64U) {
            mask = UINT64_MAX;
        } else {
            mask = (UINT64_C(1) << field->bit_width) - UINT64_C(1);
        }
        unit_bits |= (value & mask) << field->bit_offset;
        scan += 1U;
    }
    if (!minic_riscv64_emit_typed_bits(file, program, first->type, unit_bits)) {
        return false;
    }
    *cursor = unit_offset + unit_size;
    *field_index = scan - 1U;
    return true;
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

            if (*initializer_index == object->initializer_count &&
                *relocation_index == object->relocation_count) {
                if (cursor > type_size ||
                    !minic_riscv64_emit_zero_bytes(file, type_size - cursor)) {
                    return false;
                }
                *emitted_size = type_size;
                return true;
            }
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
            if (field->is_bit_field) {
                if (!minic_riscv64_emit_record_bit_field_run(file,
                                                             program,
                                                             object,
                                                             record,
                                                             field_limit,
                                                             type_size,
                                                             &field_index,
                                                             initializer_index,
                                                             relocation_index,
                                                             &cursor)) {
                    return false;
                }
                if (record->is_union) {
                    break;
                }
                continue;
            }
            if (record->is_union) {
                field_offset = 0U;
            } else if (!minic_data_layout_record_field_offset(minic_default_data_layout(),
                                                              program,
                                                              record,
                                                              field_index,
                                                              &field_offset)) {
                return false;
            }
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
    size_t object_alignment;
    size_t storage_size;

    if (!minic_data_layout_global_object(
            minic_default_data_layout(), program, object, &storage_size, &object_alignment)) {
        return false;
    }
    (void)object_alignment;

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
            !has_recursive_relocation && !minic_riscv64_record_has_bit_fields(record)) {
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
           relocation_index == object->relocation_count && emitted_size == storage_size;
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

static bool minic_riscv64_emit_recursive_array_values(FILE *file,
                                                      const MinicC0Program *program,
                                                      const MinicGlobalObject *object) {
    size_t object_alignment;
    size_t storage_size;
    size_t emitted_size;
    size_t initializer_index;
    size_t relocation_index;

    if (file == NULL || program == NULL || object == NULL || object->is_zero_initialized ||
        !minic_type_is_array(object->type) ||
        !minic_data_layout_global_object(
            minic_default_data_layout(), program, object, &storage_size, &object_alignment)) {
        return false;
    }
    (void)object_alignment;
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
           relocation_index == object->relocation_count && emitted_size == storage_size;
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

static bool minic_riscv64_zero_size_record_definition(const MinicC0Program *program,
                                                      const MinicGlobalObject *object) {
    size_t object_alignment;
    size_t storage_size;

    if (program == NULL || object == NULL ||
        !minic_data_layout_global_object(
            minic_default_data_layout(), program, object, &storage_size, &object_alignment)) {
        return false;
    }
    (void)object_alignment;

    const MinicRecord *record;

    if (storage_size != 0U || !minic_type_is_record(object->type) ||
        object->initializer_count != 0U || object->relocation_count != 0U) {
        return false;
    }
    record = minic_c0_program_record(program, object->type.record_id);
    return record != NULL && record->is_complete && record->field_count == 0U;
}

static bool minic_riscv64_emit_global_object(FILE *file,
                                             const MinicC0Program *program,
                                             const MinicGlobalObject *object) {
    size_t object_alignment;
    size_t storage_size;

    if (!minic_data_layout_global_object(
            minic_default_data_layout(), program, object, &storage_size, &object_alignment)) {
        return false;
    }

    MinicType scalar_type;
    const char *directive;
    unsigned int alignment_power;
    size_t scalar_width;
    size_t initializer_index;
    bool zero_size_record_definition;

    if (file == NULL || program == NULL || object == NULL || object->name_length == 0U ||
        object_alignment == 0U ||
        !minic_riscv64_alignment_power(object_alignment, &alignment_power)) {
        return false;
    }
    zero_size_record_definition = minic_riscv64_zero_size_record_definition(program, object);
    if (storage_size == 0U && !zero_size_record_definition) {
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
        if (record == NULL || !record->is_complete ||
            (object->initializer_count == 0U && !zero_size_record_definition)) {
            return false;
        }
    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL) ||
               (minic_type_is_array(object->type) && object->relocation_count != 0U)) {
        if (object->initializer_count == 0U) {
            return false;
        }
    } else {
        if (object->relocation_count != 0U ||
            !minic_riscv64_global_scalar_type(program, object->type, &scalar_type, &scalar_width) ||
            scalar_width == 0U || object->initializer_count > storage_size / scalar_width) {
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
        if (fprintf(file, ".globl %s\n", object->name) < 0 ||
            !minic_riscv64_emit_symbol_visibility(file, object->name, object->visibility)) {
            return false;
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
    } else if ((minic_riscv64_record_array_info(program, object->type, NULL, NULL) ||
                (minic_type_is_array(object->type) && object->relocation_count != 0U)) &&
               object->initializer_count != 0U) {
        if (!minic_riscv64_emit_recursive_array_values(file, program, object)) {
            return false;
        }
    } else if (object->relocation_count != 0U) {
        if (!emit_symbol_relocs(file, program, object)) {
            return false;
        }
    } else if (object->is_zero_initialized || object->is_tentative) {
        if (!minic_riscv64_emit_zero_bytes(file, storage_size)) {
            return false;
        }
    } else if (minic_type_is_record(object->type)) {
        if (!minic_riscv64_emit_record_values(file, program, object)) {
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
                file, storage_size - object->initializer_count * scalar_width)) {
            return false;
        }
    }
    return fprintf(file, ".size %s, %zu\n", object->name, storage_size) >= 0;
}

static bool minic_riscv64_emit_function(FILE *file,
                                        const MinicC0Program *program,
                                        const MinicFunction *function,
                                        size_t *label_counter) {
    MinicRiscv64FunctionLayout function_layout;
    MinicRiscv64FrameLayout frame_layout;
    size_t frame_size;
    bool success;
    const char *symbol_name;

    if (function == NULL || !function->is_defined || function->name_length == 0U ||
        function->body_block >= program->block_count) {
        return false;
    }
    minic_riscv64_function_layout_initialize(&function_layout);
    if (!minic_riscv64_layout_function(NULL, program, function, &function_layout, NULL)) {
        return false;
    }
    if (!minic_riscv64_frame_layout_from_function_layout(
            program, function, &function_layout, &frame_layout)) {
        minic_riscv64_function_layout_destroy(&function_layout);
        return false;
    }
    frame_size = frame_layout.frame_size;
    symbol_name = minic_c0_function_symbol_name(function);
    if (symbol_name == NULL || symbol_name[0] == '\0') {
        minic_riscv64_function_layout_destroy(&function_layout);
        return false;
    }
    success = true;
    if (function->section_name != NULL) {
        if (fprintf(file, ".section %s,\"ax\",@progbits\n", function->section_name) < 0) {
            success = false;
        }
    } else if (fputs(".text\n", file) == EOF) {
        success = false;
    }
    if (success && !function->is_static) {
        if (fprintf(file, ".globl %s\n", symbol_name) < 0 ||
            !minic_riscv64_emit_symbol_visibility(file, symbol_name, function->visibility)) {
            success = false;
        }
    }
    if (success &&
        fprintf(file,
                ".type %s, @function\n"
                "%s:\n",
                symbol_name,
                symbol_name) < 0) {
        success = false;
    }
    if (success) {
        success =
            minic_riscv64_emit_function_prologue(file, program, function, &frame_layout, &function_layout);
    }
    if (success && function->body_block < program->block_count) {
        success = minic_riscv64_emit_block(file,
                                           program,
                                           function,
                                           &frame_layout,
                                           &function_layout,
                                           function->body_block,
                                           label_counter,
                                           MINIC_STATEMENT_ID_INVALID,
                                           MINIC_STATEMENT_ID_INVALID);
    }
    if (success && fprintf(file, ".size %s, .-%s\n", symbol_name, symbol_name) < 0) {
        success = false;
    }
    minic_riscv64_function_layout_destroy(&function_layout);
    return success;
}

static bool minic_riscv64_write_file_asm(FILE *file, const MinicC0Program *program) {
    size_t index;

    if (file == NULL || program == NULL) {
        return false;
    }
    for (index = 0U; index < program->file_asm_count; ++index) {
        if (!minic_riscv64_emit_file_asm(file, &program->file_asm[index])) {
            return false;
        }
    }
    return true;
}

bool minic_riscv64_write_c0_program(const char *path, const MinicC0Program *program) {
    FILE *file;
    size_t function_index;
    size_t global_index;
    size_t label_counter;
    bool success;

    if (path == NULL || program == NULL) {
        return false;
    }
    file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }
    success = minic_riscv64_emit_string_literals(file, program) &&
              minic_riscv64_emit_compound_literal_data(file, program) &&
              minic_riscv64_write_file_asm(file, program);
    for (global_index = 0U; success && global_index < program->global_object_count; ++global_index) {
        if (!program->global_objects[global_index].is_defined) {
            continue;
        }
        success =
            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);
        if (!success) {
        }
    }
    for (function_index = 0U; success && function_index < program->function_count;
         ++function_index) {
        const MinicFunction *function = &program->functions[function_index];

        if (!function->is_defined) {
            continue;
        }
        success = minic_riscv64_emit_function(file, program, function, &label_counter);
        if (!success) {
        }
    }
    if (fclose(file) != 0) {
        success = false;
    }
    if (!success) {
        (void)remove(path);
    }
    return success;
}
