/* Source-free Core record verifier: reject physically impossible member spans
 * before any target ABI planner or Machine IR selector trusts their offsets. */
#include "core/core_type.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static bool primitive(MinicCoreTypeContext *ctx, MinicCoreTypeKind kind,
                      size_t bytes, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = kind;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = bytes;
    if (kind == MINIC_CORE_TYPE_INTEGER) type.value.integer.bit_width = (unsigned)(bytes * 8U);
    return minic_core_type_register(ctx, &type, id);
}

static bool invalid_record(MinicCoreTypeContext *ctx, MinicCoreTypeId field_ty,
                           size_t byte_offset, size_t bit_offset,
                           size_t bit_width, bool bit_field) {
    MinicCoreRecordField field = {0};
    MinicCoreType record = {0};
    MinicCoreTypeId ignored;
    field.type_id = field_ty;
    field.byte_offset = byte_offset;
    field.bit_offset = bit_offset;
    field.bit_width = bit_width;
    field.alignment = 4U;
    field.is_bit_field = bit_field;
    record.kind = MINIC_CORE_TYPE_RECORD;
    record.has_layout = true;
    record.size = 8U;
    record.alignment = 4U;
    record.value.record.fields = &field;
    record.value.record.field_count = 1U;
    record.value.record.is_complete = true;
    return minic_core_type_register(ctx, &record, &ignored);
}

int main(void) {
    MinicCoreTypeContext ctx = {0};
    MinicCoreTypeId u32, rec, incomplete_array;
    MinicCoreType prov = {0}, complete = {0}, array = {0};
    MinicCoreRecordField fields[2] = {{0}, {0}};
    minic_core_type_context_initialize(&ctx);
    if (!primitive(&ctx, MINIC_CORE_TYPE_INTEGER, 4U, &u32)) return 1;
    /* All five of these malformed complete record layouts were previously
     * accepted by the CoreType registration boundary. */
    if (invalid_record(&ctx, u32, 9U, 0U, 0U, false)) return 2;
    if (invalid_record(&ctx, u32, 6U, 0U, 0U, false)) return 3;
    if (invalid_record(&ctx, u32, 0U, 8U, 3U, true)) return 4;
    if (invalid_record(&ctx, u32, 0U, 0U, 33U, true)) return 5;
    if (invalid_record(&ctx, u32, 0U, 1U, 0U, false)) return 6;
    /* A provisional record's Source-resolved layout may already be known;
     * completing its Core member graph must preserve that contract. */
    prov.kind = MINIC_CORE_TYPE_RECORD;
    prov.has_layout = true;
    prov.size = 8U;
    prov.alignment = 4U;
    if (!minic_core_type_register(&ctx, &prov, &rec)) return 7;
    fields[0].type_id = u32;
    fields[0].alignment = 4U;
    fields[1].type_id = u32;
    fields[1].byte_offset = 4U;
    fields[1].alignment = 4U;
    complete = prov;
    complete.value.record.is_complete = true;
    complete.value.record.field_count = 2U;
    complete.value.record.fields = fields;
    if (!minic_core_type_complete_record(&ctx, rec, &complete)) return 8;
    /* Legitimate incomplete flexible array in a final record slot. */
    array.kind = MINIC_CORE_TYPE_ARRAY;
    array.value.array.element = u32;
    if (!minic_core_type_register(&ctx, &array, &incomplete_array)) return 9;
    fields[0].type_id = u32;
    fields[0].byte_offset = 0U;
    fields[0].alignment = 4U;
    fields[1].type_id = incomplete_array;
    fields[1].byte_offset = 8U;
    fields[1].alignment = 4U;
    complete.value.record.fields = fields;
    if (!minic_core_type_register(&ctx, &complete, &rec)) return 10;
    minic_core_type_context_destroy(&ctx);
    puts("MINIC_CORE_RECORD_LAYOUT=PASS invalid_field_spans=5 flexible_tail=1");
    return 0;
}
