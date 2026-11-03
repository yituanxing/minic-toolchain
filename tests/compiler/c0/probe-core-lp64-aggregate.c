#include "target/common/aggregate_shape.h"
#include "target/common/lp64_aggregate.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        (void)fprintf(stderr, "CORE_LP64_AGGREGATE=FAIL line=%d: %s\n", \
                      __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static bool integer(MinicCoreTypeContext *ctx, size_t bytes, MinicCoreTypeId *out) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = bytes;
    type.value.integer.bit_width = (unsigned int)(bytes * 8U);
    return minic_core_type_register(ctx, &type, out);
}

static bool scalar_float(MinicCoreTypeContext *ctx, size_t bytes,
                         MinicCoreTypeId *out) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = bytes;
    type.value.floating.format = bytes == 4U
        ? MINIC_CORE_FLOAT_BINARY32 : MINIC_CORE_FLOAT_BINARY64;
    return minic_core_type_register(ctx, &type, out);
}

static bool record(MinicCoreTypeContext *ctx, size_t bytes, size_t align,
                   const MinicCoreRecordField *fields, size_t count,
                   bool is_union, size_t pack_align, size_t explicit_align,
                   MinicCoreTypeId *out) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = align;
    type.value.record.is_complete = true;
    type.value.record.is_union = is_union;
    type.value.record.has_nondefault_layout_policy =
        pack_align != 0U || explicit_align != 0U;
    type.value.record.fields = (MinicCoreRecordField *)fields;
    type.value.record.field_count = count;
    return minic_core_type_register(ctx, &type, out);
}

int main(void) {
    MinicCoreTypeContext ctx;
    MinicCoreType type = {0};
    MinicCoreTypeId i8, i32, i64, f32, f64, arr12, zero_arr, unsized_arr;
    MinicCoreTypeId int_record, packed, misaligned, empty, nested;
    MinicCoreTypeId array_record, fp_record, packed_fp, field_restricted_fp;
    MinicCoreTypeId extended_fp, union_fp;
    MinicCoreTypeId big_record, huge_record, qual_record, vector_id, vector_record;
    MinicCoreRecordField fields[2] = {{0}};
    MinicAggregateStorageShape shape;
    size_t bytes, chunks, lanes, width;

    minic_core_type_context_initialize(&ctx);
    CHECK(integer(&ctx, 1U, &i8));
    CHECK(integer(&ctx, 4U, &i32));
    CHECK(integer(&ctx, 8U, &i64));
    CHECK(scalar_float(&ctx, 4U, &f32));
    CHECK(scalar_float(&ctx, 8U, &f64));

    fields[0].type_id = i32;
    fields[0].alignment = 4U;
    CHECK(record(&ctx, 4U, 4U, fields, 1U, false, 0U, 0U, &int_record));
    CHECK(minic_multi_gp_record_core(&ctx, int_record, &bytes, &chunks));
    CHECK(bytes == 4U && chunks == 1U);
    CHECK(!minic_multi_empty_record_core(&ctx, int_record));
    CHECK(!minic_multi_indirect_record_core(&ctx, int_record, &bytes));
    CHECK(!minic_multi_fp_return_record_core(&ctx, int_record, &lanes, &width));

    fields[0].type_id = i8;
    fields[0].alignment = 1U;
    fields[0].byte_offset = 0U;
    fields[1].type_id = i32;
    fields[1].byte_offset = 1U;
    fields[1].is_bit_field = true;
    fields[1].bit_width = 3U;
    fields[1].alignment = 1U;
    CHECK(record(&ctx, 5U, 1U, fields, 2U, false, 1U, 0U, &packed));
    CHECK(minic_multi_gp_record_core(&ctx, packed, &bytes, &chunks));
    CHECK(bytes == 5U && chunks == 1U);
    fields[1].is_bit_field = false;
    fields[1].bit_width = 0U;
    CHECK(record(&ctx, 5U, 1U, fields, 2U, false, 1U, 0U, &misaligned));
    CHECK(!minic_multi_gp_record_core(&ctx, misaligned, &bytes, &chunks));

    CHECK(record(&ctx, 0U, 1U, NULL, 0U, false, 0U, 0U, &empty));
    CHECK(minic_multi_empty_record_core(&ctx, empty));
    CHECK(!minic_multi_gp_record_core(&ctx, empty, &bytes, &chunks));
    fields[0] = (MinicCoreRecordField){.type_id = empty, .alignment = 1U};
    fields[1] = (MinicCoreRecordField){.type_id = int_record,
                                        .byte_offset = 0U, .alignment = 4U};
    CHECK(record(&ctx, 4U, 4U, fields, 2U, false, 0U, 0U, &nested));
    CHECK(minic_multi_gp_record_core(&ctx, nested, &bytes, &chunks));
    CHECK(bytes == 4U && chunks == 1U);

    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_ARRAY;
    type.has_layout = true;
    type.size = 12U;
    type.alignment = 4U;
    type.value.array.element = i32;
    type.value.array.count = 3U;
    type.value.array.is_complete = true;
    CHECK(minic_core_type_register(&ctx, &type, &arr12));
    type.value.array.count = 0U;
    type.size = 0U;
    CHECK(minic_core_type_register(&ctx, &type, &zero_arr));
    type.value.array.is_complete = false;
    type.has_layout = false;
    type.alignment = 0U;
    CHECK(minic_core_type_register(&ctx, &type, &unsized_arr));
    fields[0] = (MinicCoreRecordField){.type_id = zero_arr, .alignment = 4U};
    CHECK(record(&ctx, 8U, 4U, fields, 1U, false, 0U, 0U, &array_record));
    CHECK(!minic_multi_gp_record_core(&ctx, array_record, &bytes, &chunks));
    fields[0].type_id = unsized_arr;
    CHECK(record(&ctx, 8U, 4U, fields, 1U, false, 0U, 0U, &array_record));
    CHECK(!minic_multi_gp_record_core(&ctx, array_record, &bytes, &chunks));
    fields[0] = (MinicCoreRecordField){.type_id = arr12, .alignment = 4U};
    CHECK(record(&ctx, 12U, 4U, fields, 1U, false, 0U, 0U, &array_record));
    CHECK(minic_multi_gp_record_core(&ctx, array_record, &bytes, &chunks));
    CHECK(bytes == 12U && chunks == 2U);

    fields[0] = (MinicCoreRecordField){.type_id = f32, .alignment = 4U};
    fields[1] = (MinicCoreRecordField){.type_id = f32,
                                        .byte_offset = 4U, .alignment = 4U};
    CHECK(record(&ctx, 8U, 4U, fields, 2U, false, 0U, 0U, &fp_record));
    CHECK(minic_multi_fp_return_record_core(&ctx, fp_record, &lanes, &width));
    CHECK(lanes == 2U && width == 4U);
    CHECK(!minic_multi_gp_record_core(&ctx, fp_record, &bytes, &chunks));
    fields[0].has_nondefault_layout_policy = true;
    CHECK(record(&ctx, 8U, 4U, fields, 2U, false, 0U, 0U, &field_restricted_fp));
    CHECK(!minic_multi_fp_return_record_core(&ctx, field_restricted_fp, &lanes, &width));
    fields[0].has_nondefault_layout_policy = false;
    CHECK(record(&ctx, 8U, 4U, fields, 2U, false, 1U, 0U, &packed_fp));
    CHECK(!minic_multi_fp_return_record_core(&ctx, packed_fp, &lanes, &width));
    CHECK(record(&ctx, 8U, 4U, fields, 2U, false, 0U, 8U, &extended_fp));
    CHECK(!minic_multi_fp_return_record_core(&ctx, extended_fp, &lanes, &width));
    CHECK(record(&ctx, 8U, 4U, fields, 2U, true, 0U, 0U, &union_fp));
    CHECK(!minic_multi_fp_return_record_core(&ctx, union_fp, &lanes, &width));

    fields[0] = (MinicCoreRecordField){.type_id = i64, .alignment = 8U};
    CHECK(record(&ctx, 24U, 8U, fields, 1U, false, 0U, 0U, &big_record));
    CHECK(minic_multi_indirect_record_core(&ctx, big_record, &bytes));
    CHECK(bytes == 24U);
    CHECK(record(&ctx, 2049U, 8U, fields, 1U, false, 0U, 0U, &huge_record));
    CHECK(!minic_multi_indirect_record_core(&ctx, huge_record, &bytes));
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_QUALIFIED;
    type.value.qualified.base = big_record;
    type.has_layout = true;
    type.size = 24U;
    type.alignment = 16U;
    type.has_abi_alignment_override = true;
    CHECK(minic_core_type_register(&ctx, &type, &qual_record));
    CHECK(minic_common_aggregate_storage_shape_core(&ctx, qual_record, 8U, &shape));
    CHECK(shape.alignment == 16U && shape.words == 3U);
    CHECK(minic_multi_indirect_record_core(&ctx, qual_record, &bytes));

    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_VECTOR;
    type.has_layout = true;
    type.size = 16U;
    type.alignment = 16U;
    type.value.vector.element = i32;
    type.value.vector.element_count = 4U;
    CHECK(minic_core_type_register(&ctx, &type, &vector_id));
    fields[0] = (MinicCoreRecordField){.type_id = vector_id, .alignment = 16U};
    CHECK(record(&ctx, 16U, 16U, fields, 1U, false, 0U, 0U, &vector_record));
    CHECK(!minic_multi_gp_record_core(&ctx, vector_record, &bytes, &chunks));

    CHECK(!minic_multi_gp_record_core(&ctx, MINIC_CORE_TYPE_INVALID,
                                      &bytes, &chunks));
    CHECK(!minic_multi_indirect_record_core(&ctx, MINIC_CORE_TYPE_INVALID, &bytes));
    CHECK(!minic_common_aggregate_storage_shape_core(
        &ctx, MINIC_CORE_TYPE_INVALID, 8U, &shape));
    CHECK(!minic_common_aggregate_storage_shape_core(&ctx, int_record, 0U, &shape));
    CHECK(!minic_common_aggregate_storage_shape_core(&ctx, int_record, 3U, &shape));
    CHECK(!minic_common_aggregate_storage_shape_core(NULL, int_record, 8U, &shape));

    minic_core_type_context_destroy(&ctx);
    (void)puts("CORE_LP64_AGGREGATE=PASS integer bitfield packed unaligned empty nested array hfa indirect qualifier vector invalid");
    return 0;
}
