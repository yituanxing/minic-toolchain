#include "target/common/aggregate_shape.h"

/* Single DataLayout-backed record shape for all five target profiles.
 * No architecture/ABI branch or physical-register knowledge belongs here. */
bool minic_common_aggregate_storage_shape(
    const MinicC0Program *program, const MinicDataLayout *layout,
    MinicType type, size_t word_bytes, MinicAggregateStorageShape *out) {
    MinicAggregateStorageShape shape;
    if (program == NULL || layout == NULL || out == NULL ||
        !minic_type_is_record(type) || word_bytes == 0U ||
        (word_bytes & (word_bytes - 1U)) != 0U ||
        !minic_data_layout_type(layout, program, type,
                                &shape.bytes, &shape.alignment) ||
        shape.alignment == 0U)
        return false;
    /* ceil(bytes/word) without overflow; GNU empty records take 0 words. */
    shape.words = shape.bytes / word_bytes +
                  (shape.bytes % word_bytes != 0U ? 1U : 0U);
    *out = shape;
    return true;
}

/* Originally duplicated in the RV64 ABI planner. A structural question
 * is not an RV64 machine-code/ABI question. Keep this permissive relative
 * to each ISA's target-owned stricter eligibility and placement rules. */
static bool integral_tree(const MinicC0Program *program, MinicType type,
                          unsigned int depth) {
    const MinicRecord *record;
    const MinicArrayType *array;
    size_t i;
    if (program == NULL || depth > 256U) return false;
    if (minic_type_is_integer(type) || minic_type_is_pointer(type))
        return true;
    if (minic_type_is_array(type)) {
        array = minic_c0_program_array_type(program, type.array_type_id);
        return array != NULL &&
               integral_tree(program, array->element_type, depth + 1U);
    }
    if (!minic_type_is_record(type)) return false;
    record = minic_c0_program_record(program, type.record_id);
    if (record == NULL || !record->is_complete) return false;
    for (i = 0U; i < record->field_count; ++i) {
        const MinicRecordField *field = minic_c0_record_field(record, i);
        if (field == NULL ||
            !integral_tree(program, field->type, depth + 1U))
            return false;
    }
    return true;
}

bool minic_common_aggregate_integral_tree(const MinicC0Program *program,
                                           MinicType type) {
    return integral_tree(program, type, 0U);
}
