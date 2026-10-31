#include "frontend/ast.h"
#include "frontend/type.h"
#include "target/data_layout.h"

#include <stdio.h>
#include <string.h>

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
    bool valid_record = false, invalid_array = false;
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

    minic_c0_program_destroy(&program);
    if (!valid_record || !invalid_array) {
        (void)fprintf(stderr,
            "FAIL zero-sized aggregate relocation: record=%d array=%d\n",
            valid_record ? 1 : 0, invalid_array ? 1 : 0);
        return 1;
    }
    (void)puts("DATA_LAYOUT_ZERO_SIZE_RELOC=PASS record_offset=8 array_no_slots=1");
    return 0;
}
