#include "frontend/ast.h"
#include "frontend/type.h"
#include "target/data_layout.h"
#include "target/data_layout_source.h"

#include <stdio.h>
#include <string.h>

/* An explicit field extent and an existing nested ArrayType must converge to
 * the same materialized field shape, including aggregate relocation slots. */
static bool check_record_array_shape_equivalence(MinicC0Program *program,
                                                 const MinicDataLayout *layout) {
    MinicRecordId legacy_id, materialized_id, zero_id;
    MinicType pointer_type, pointer_array, zero_array, inner_array, outer_array;
    const MinicRecord *legacy, *materialized;
    MinicGlobalObject object;
    MinicGlobalRelocation relocation;
    size_t legacy_size, materialized_size, legacy_align, materialized_align;
    size_t offset;
    if (!minic_type_pointer_to(minic_type_long(), &pointer_type) ||
        !minic_c0_program_add_array_type(program, pointer_type, 2U, &pointer_array) ||
        !minic_c0_program_add_array_type(program, minic_type_int(), 3U, &inner_array) ||
        !minic_c0_program_add_array_type(program, inner_array, 2U, &outer_array) ||
        !minic_c0_program_add_zero_length_array_type(
            program, minic_type_int(), &zero_array) ||
        !minic_c0_program_add_record(program, "LegacyArray", 11U, &legacy_id) ||
        !minic_c0_record_add_field(program, legacy_id, "pair", 4U,
                                   pointer_type, 2U) ||
        !minic_c0_record_add_field(program, legacy_id, "rows", 4U,
                                   inner_array, 2U) ||
        !minic_c0_program_finish_record(program, legacy_id) ||
        !minic_c0_program_add_record(program, "MaterializedArray", 17U,
                                     &materialized_id) ||
        !minic_c0_record_add_field(program, materialized_id, "pair", 4U,
                                   pointer_array, 1U) ||
        !minic_c0_record_add_field(program, materialized_id, "rows", 4U,
                                   outer_array, 1U) ||
        !minic_c0_program_finish_record(program, materialized_id) ||
        !minic_c0_program_add_record(program, "ZeroTail", 8U, &zero_id) ||
        !minic_c0_record_add_field(program, zero_id, "before", 6U,
                                   minic_type_char(), 1U) ||
        !minic_c0_record_add_field(program, zero_id, "empty", 5U,
                                   zero_array, 1U) ||
        !minic_c0_record_add_field(program, zero_id, "after", 5U,
                                   pointer_type, 1U) ||
        !minic_c0_program_finish_record(program, zero_id))
        return false;
    legacy = minic_c0_program_record(program, legacy_id);
    materialized = minic_c0_program_record(program, materialized_id);
    if (legacy == NULL || materialized == NULL ||
        !minic_data_layout_type(layout, program, minic_type_record(legacy_id),
                                &legacy_size, &legacy_align) ||
        !minic_data_layout_type(layout, program, minic_type_record(materialized_id),
                                &materialized_size, &materialized_align) ||
        legacy_size != materialized_size || legacy_size != 40U ||
        legacy_align != materialized_align || legacy_align != 8U ||
        !minic_data_layout_record_field_offset(
            layout, program, legacy, 1U, &offset) || offset != 16U ||
        !minic_data_layout_record_field_offset(
            layout, program, materialized, 1U, &offset) || offset != 16U ||
        !minic_data_layout_type(layout, program, minic_type_record(zero_id),
                                &materialized_size, &materialized_align) ||
        materialized_size != 16U || materialized_align != 8U ||
        !minic_data_layout_record_field_offset(
            layout, program, minic_c0_program_record(program, zero_id),
            2U, &offset) || offset != 8U)
        return false;
    (void)memset(&object, 0, sizeof(object));
    (void)memset(&relocation, 0, sizeof(relocation));
    relocation.location_kind = MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR;
    relocation.location_index = 1U;
    object.type = minic_type_record(legacy_id);
    if (!minic_data_layout_global_relocation_offset(
            layout, program, &object, &relocation, &offset) || offset != 8U)
        return false;
    object.type = minic_type_record(materialized_id);
    return minic_data_layout_global_relocation_offset(
               layout, program, &object, &relocation, &offset) && offset == 8U;
}

/* Exercising the public DataLayout relocation API is intentional: the normal
 * global emitter can choose a different, direct initializer walker which
 * never visits this source-location resolver. */
int main(void) {
    MinicC0Program program;
    MinicRecordId empty_id, holder_id;
    MinicType empty_array_type, pointer_type;
    MinicGlobalObject object;
    MinicGlobalRelocation reloc;
    size_t offset, bytes, alignment, empty_bytes, empty_alignment;
    bool valid_record = false, invalid_array = false, matching_field_shapes = false;
    const MinicDataLayout *layout = minic_x86_64_linux_data_layout();

    minic_c0_program_initialize(&program);
    memset(&object, 0, sizeof(object));
    memset(&reloc, 0, sizeof(reloc));
    if (!minic_c0_program_add_record(&program, "Empty", 5U, &empty_id) ||
        !minic_c0_program_finish_record(&program, empty_id) ||
        !minic_c0_program_add_array_type(
            &program, minic_type_record(empty_id), 2U, &empty_array_type) ||
        !minic_type_pointer_to(minic_type_long(), &pointer_type) ||
        !minic_c0_program_add_record(&program, "Holder", 6U, &holder_id) ||
        !minic_c0_record_add_field(&program, holder_id, "prefix", 6U,
                                   minic_type_int(), 1U) ||
        !minic_c0_record_add_field(&program, holder_id, "ignored", 7U,
                                   minic_type_record(empty_id), 2U) ||
        !minic_c0_record_add_field(&program, holder_id, "ptr", 3U,
                                   pointer_type, 1U) ||
        !minic_c0_program_finish_record(&program, holder_id)) {
        minic_c0_program_destroy(&program);
        return 2;
    }
    reloc.location_kind = MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR;
    /* A pure empty-record array has no scalar slots. A fabricated
     * relocation slot must fail normally, never divide by zero. */
    object.type = empty_array_type;
    reloc.location_index = 0U;
    invalid_array = !minic_data_layout_global_relocation_offset(
        layout, &program, &object, &reloc, &offset);

    reloc.location_index = 1U; /* integer prefix is slot 0, pointer is slot 1 */
    object.type = minic_type_record(holder_id);
    valid_record = minic_data_layout_type(
                       layout, &program, minic_type_record(empty_id),
                       &empty_bytes, &empty_alignment) && empty_bytes == 0U &&
        minic_data_layout_global_object(
                       layout, &program, &object, &bytes, &alignment) &&
        bytes == 16U && alignment == 8U &&
        minic_data_layout_global_relocation_offset(
            layout, &program, &object, &reloc, &offset) && offset == 8U;

    matching_field_shapes = check_record_array_shape_equivalence(&program, layout);

    minic_c0_program_destroy(&program);
    if (!valid_record || !invalid_array || !matching_field_shapes) {
        (void)fprintf(stderr,
            "FAIL zero-sized aggregate relocation: record=%d array=%d field-shapes=%d\n",
            valid_record ? 1 : 0, invalid_array ? 1 : 0,
            matching_field_shapes ? 1 : 0);
        return 1;
    }
    (void)puts("DATA_LAYOUT_ZERO_SIZE_RELOC=PASS record_offset=8 array_no_slots=1 field_shape_equivalence=1");
    return 0;
}
