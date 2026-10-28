#include "target/riscv64/codegen.h"
#include "target/riscv64/global_emit.h"
#include "target/riscv64/isa.h"
#include "target/common/global_emit.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/core_codegen.h"
#include "target/riscv/function_symbol.h"
#include "target/data_layout.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type) &&
         !minic_type_is_float(type) && !minic_type_is_double(type)) ||
        !minic_data_layout_type(minic_default_data_layout(), program, type, &width, &alignment) ||
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
    if (width == 1U || minic_type_is_float(type) || minic_type_is_double(type) ||
        (minic_type_is_integer(type) && minic_type_is_unsigned_integer(type))) {
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

/* Shared layout emits via RV64's existing typed GNU scalar spelling. */
static bool minic_riscv64_emit_shared_bits(
    FILE *file, const MinicC0Program *program,
    const MinicTargetInfo *target, MinicType type, uint64_t bits) {
    return target != NULL && target->architecture == MINIC_TARGET_ARCH_RISCV64 &&
           minic_riscv64_emit_typed_bits(file, program, type, bits);
}

static bool minic_riscv64_global_scalar_type(const MinicC0Program *program,
                                             MinicType object_type,
                                             MinicType *scalar_type,
                                             size_t *scalar_width) {
    MinicType type;
    size_t alignment;

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
    if ((!minic_type_is_integer(type) && !minic_type_is_pointer(type) &&
         !minic_type_is_float(type) && !minic_type_is_double(type)) ||
        !minic_data_layout_type(minic_default_data_layout(), program, type, scalar_width, &alignment) ||
        (*scalar_width != 1U && *scalar_width != 2U && *scalar_width != 4U &&
         *scalar_width != 8U)) {
        return false;
    }
    (void)alignment;
    *scalar_type = type;
    return true;
}

static bool minic_riscv64_emit_zero_bytes(FILE *file, size_t size) {
    return size == 0U || fprintf(file, "  .zero %zu\n", size) >= 0;
}

static bool minic_riscv64_emit_symbol_value(FILE *file,
                                            const MinicC0Program *program,
                                            const MinicGlobalRelocation *relocation,
                                            size_t width) {
    const char *directive;
    const char *target_name;
    int64_t target_addend;

    directive = minic_riscv64_integer_data_directive(width);
    if (file == NULL || directive == NULL) {
        return false;
    }
    if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_LABEL) {
        const MinicStatement *label;

        label = minic_c0_program_statement(program, relocation->target_id);
        return label != NULL && label->kind == MINIC_STATEMENT_LABEL &&
               fprintf(file, "  %s .Luser_%zu\n", directive, relocation->target_id) >= 0;
    }
    target_name = minic_multi_global_relocation_target_name(program, relocation);
    if (target_name == NULL || target_name[0] == '\0' ||
        !minic_data_layout_global_relocation_target_addend(
            minic_default_data_layout(), program, relocation, &target_addend)) {
        return false;
    }
    if (target_addend == 0U) {
        return fprintf(file, "  %s %s\n", directive, target_name) >= 0;
    }
    if (target_addend > 0) {
        return fprintf(file, "  %s %s+%" PRId64 "\n", directive, target_name, target_addend) >= 0;
    }
    return fprintf(file, "  %s %s%" PRId64 "\n", directive, target_name, target_addend) >= 0;
}

/* Reuse the proven common zero-relocation byte walk.  Keep RV64's .dword
 * spelling and expression syntax behind a narrow target-owned callback. */
static bool minic_riscv64_emit_symbol_relocation(
    FILE *file, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalRelocation *relocation, size_t width) {
    return target != NULL && target->architecture == MINIC_TARGET_ARCH_RISCV64 &&
           minic_riscv64_emit_symbol_value(file, program, relocation, width);
}

static bool
emit_symbol_relocs(FILE *file, const MinicC0Program *program, const MinicGlobalObject *object) {
    const MinicTargetInfo *target =
        minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64);
    size_t size, alignment;

    if (file == NULL || program == NULL || object == NULL || target == NULL ||
        !minic_data_layout_global_object(
            target->data_layout, program, object, &size, &alignment))
        return false;
    (void)alignment;
    return minic_multi_emit_zero_relocation_global(
        file, program, target, object, size,
        minic_riscv64_emit_symbol_relocation);
}

static bool minic_riscv64_record_has_bit_fields(const MinicRecord *record) {
    size_t field_index;

    if (record == NULL) {
        return false;
    }
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        if (record->fields[field_index].is_bit_field) {
            return true;
        }
    }
    return false;
}

/* Full recursive GlobalObject layout is now owned by common/global_emit.c:
 * nested arrays, selected unions, bitfields, GNU flexible tails and empty
 * records all use one DataLayout/slot walk. RV64 only formats scalar data
 * directives and symbol relocations. No parallel fallback algorithm. */
static bool minic_riscv64_emit_complete_recursive_global(
    FILE *file, const MinicC0Program *program, const MinicGlobalObject *object) {
    const MinicTargetInfo *target =
        minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64);
    size_t storage_size, alignment;
    if (file == NULL || program == NULL || object == NULL || target == NULL ||
        !minic_data_layout_global_object(
            target->data_layout, program, object, &storage_size, &alignment))
        return false;
    (void)alignment;
    return minic_multi_emit_recursive_aggregate_global(
        file, program, target, object, storage_size,
        minic_riscv64_emit_symbol_relocation,
        minic_riscv64_emit_shared_bits);
}

static bool minic_riscv64_emit_record_values(FILE *file,
                                             const MinicC0Program *program,
                                             const MinicGlobalObject *object) {
    const MinicRecord *record;

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
        bool has_nonscalar_field;
        size_t index;

        has_recursive_relocation = false;
        for (index = 0U; index < object->relocation_count; ++index) {
            if (object->relocations[index].location_kind ==
                MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) {
                has_recursive_relocation = true;
                break;
            }
        }
        has_nonscalar_field = false;
        for (index = 0U; index < record->field_count; ++index) {
            const MinicRecordField *field;

            field = minic_c0_record_field(record, index);
            if (field == NULL || field->element_count != 1U ||
                (!minic_type_is_integer(field->type) && !minic_type_is_pointer(field->type))) {
                has_nonscalar_field = true;
                break;
            }
        }
        if (!record->is_union && object->initializer_count == record->field_count &&
            !has_recursive_relocation && !has_nonscalar_field &&
            !minic_riscv64_record_has_bit_fields(record)) {
            const MinicTargetInfo *target =
                minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64);
            size_t storage_size, object_alignment;
            if (target == NULL ||
                !minic_data_layout_global_object(
                    target->data_layout, program, object,
                    &storage_size, &object_alignment))
                return false;
            (void)object_alignment;
            return minic_multi_emit_flat_record_global(
                file, program, target, object, storage_size,
                minic_riscv64_emit_symbol_relocation,
                minic_riscv64_emit_shared_bits);
        }
    }
    return minic_riscv64_emit_complete_recursive_global(file, program, object);
}

static bool minic_riscv64_array_uses_recursive_initializer(
    const MinicC0Program *program, MinicType type) {
    const MinicArrayType *array_type;

    if (program == NULL || !minic_type_is_array(type)) {
        return false;
    }
    array_type = minic_c0_program_array_type(program, type.array_type_id);
    return array_type != NULL &&
           (minic_type_is_record(array_type->element_type) ||
            minic_type_is_array(array_type->element_type));
}

/* Only homogeneous scalar relocations are eligible for the common slot
 * walker; nested records, unions and arrays still use the RV64 recursion. */
static bool minic_riscv64_is_flat_relocation_array(
    const MinicC0Program *program, const MinicGlobalObject *object) {
    const MinicArrayType *array;
    if (program == NULL || object == NULL || !minic_type_is_array(object->type) ||
        object->relocation_count == 0U)
        return false;
    array = minic_c0_program_array_type(program, object->type.array_type_id);
    return array != NULL &&
           (minic_type_is_integer(array->element_type) ||
            minic_type_is_pointer(array->element_type));
}

static bool minic_riscv64_emit_recursive_array_values(
    FILE *file, const MinicC0Program *program, const MinicGlobalObject *object) {
    if (file == NULL || program == NULL || object == NULL ||
        object->is_zero_initialized || !minic_type_is_array(object->type))
        return false;
    return minic_riscv64_emit_complete_recursive_global(file, program, object);
}

bool minic_riscv64_emit_file_asm(FILE *file, const MinicFileAsm *file_asm) {
    if (file == NULL || file_asm == NULL ||
        !minic_riscv64_source_asm_marker_safe(file_asm->text, file_asm->length)) {
        return false;
    }
    if (file_asm->length != 0U &&
        fwrite(file_asm->text, 1U, file_asm->length, file) != file_asm->length) {
        return false;
    }
    return fputc('\n', file) != EOF;
}

static bool minic_riscv64_zero_size_object_definition(const MinicC0Program *program,
                                                      const MinicGlobalObject *object) {
    size_t object_alignment;
    size_t storage_size;

    if (program == NULL || object == NULL ||
        !minic_data_layout_global_object(
            minic_default_data_layout(), program, object, &storage_size, &object_alignment)) {
        return false;
    }
    (void)object_alignment;
    if (storage_size != 0U || object->initializer_count != 0U || object->relocation_count != 0U) {
        return false;
    }
    if (minic_type_is_record(object->type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, object->type.record_id);
        return record != NULL && record->is_complete && record->field_count == 0U;
    }
    if (minic_type_is_array(object->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, object->type.array_type_id);
        return array_type != NULL &&
               (array_type->is_zero_length || array_type->element_count != 0U);
    }
    return false;
}

bool minic_riscv64_emit_global_object(FILE *file,
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
    bool zero_size_object_definition;

    if (file == NULL || program == NULL || object == NULL || object->name_length == 0U ||
        object_alignment == 0U ||
        !minic_riscv64_alignment_power(object_alignment, &alignment_power)) {
        return false;
    }
    zero_size_object_definition = minic_riscv64_zero_size_object_definition(program, object);
    if (storage_size == 0U && !zero_size_object_definition) {
        return false;
    }

    directive = NULL;
    scalar_width = 0U;
    if (zero_size_object_definition) {
        if (object->initializer_count != 0U || object->relocation_count != 0U) {
            return false;
        }
    } else if (object->is_zero_initialized || object->is_tentative) {
        if (object->initializer_count != 0U) {
            return false;
        }
    } else if (minic_type_is_record(object->type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete ||
            (object->initializer_count == 0U && !zero_size_object_definition)) {
            return false;
        }
    } else if (minic_riscv64_array_uses_recursive_initializer(program, object->type) ||
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
        if (!minic_multi_elf_safe_section_name(object->section_name))
            return false;
        if (fprintf(file,
                    ".section %s,\"%s\",@progbits\n",
                    object->section_name,
                    object->is_read_only ? "a" : "aw") < 0) {
            return false;
        }
    } else {
        const char *storage_section;

        /* A const object containing link-time addresses is only read-only after
           dynamic relocation.  Keep pure constants in .rodata, but place
           relocation-bearing const data in the conventional writable relocation
           section so a PIE loader can apply R_RISCV_RELATIVE/R_RISCV_64 without
           writing into the executable read-only LOAD segment. */
        storage_section =
            object->is_read_only
                ? (object->relocation_count != 0U ? ".section .data.rel.ro"
                                                  : ".section .rodata")
                : ".data";
        if (fprintf(file, "%s\n", storage_section) < 0) {
            return false;
        }
    }
    if (!object->is_internal) {
        if (fprintf(file, object->is_weak ? ".weak %s\n" : ".globl %s\n", object->name) < 0) {
            return false;
        }
        if (!minic_multi_emit_elf_symbol_visibility(
                file, object->visibility, object->name))
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
    if (zero_size_object_definition) {
        /* GNU zero-length objects still own a symbol and type, but have no
           payload bytes. The label plus .size 0 is the complete definition. */
    } else if (minic_type_is_record(object->type) && object->initializer_count != 0U) {
        if (!minic_riscv64_emit_record_values(file, program, object)) {
            return false;
        }
    } else if (minic_riscv64_is_flat_relocation_array(program, object) &&
               object->initializer_count != 0U) {
        const MinicTargetInfo *target =
            minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64);
        if (target == NULL ||
            !minic_multi_emit_relocatable_scalar_array(
                file, program, target, object, storage_size,
                minic_riscv64_emit_symbol_relocation,
                minic_riscv64_emit_shared_bits))
            return false;
    } else if ((minic_riscv64_array_uses_recursive_initializer(program, object->type) ||
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

