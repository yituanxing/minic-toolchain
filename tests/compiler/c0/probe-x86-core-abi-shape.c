#include "core/core_type.h"
#include "target/x86_64/abi.h"
#include <stdio.h>
#include <string.h>

#define REQUIRE(condition) do { if (!(condition)) { \
    (void)fprintf(stderr, "X86_CORE_ABI_SHAPE=FAIL line=%d: %s\n", \
                  __LINE__, #condition); \
    return 1; \
} } while (0)

static bool put(MinicCoreTypeContext *types, MinicCoreType type,
                MinicCoreTypeId *id) {
    return minic_core_type_register(types, &type, id);
}

int main(void) {
    MinicCoreTypeContext context;
    MinicCoreType type = {0};
    MinicCoreTypeId integer, pointer, array, one, two, unaligned, large, small;
    MinicCoreTypeId aligned_alias, oversized_alias, incomplete;
    MinicCoreTypeId zero_array, unsized_array, zero_tail, unsized_tail;
    MinicCoreRecordField fields[2] = {{0}};
    size_t bytes = 0U;
    bool sign = false;

    minic_core_type_context_initialize(&context);
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    REQUIRE(put(&context, type, &integer));
    REQUIRE(minic_x86_64_core_scalar_width(&context, integer, &bytes, &sign));
    REQUIRE(bytes == 4U && sign);

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = integer;
    REQUIRE(put(&context, type, &pointer));
    REQUIRE(minic_x86_64_core_scalar_width(&context, pointer, &bytes, &sign));
    REQUIRE(bytes == 8U && !sign);

    fields[0].type_id = integer;
    fields[0].alignment = 4U;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.record.is_complete = true;
    type.value.record.fields = fields;
    type.value.record.field_count = 1U;
    REQUIRE(put(&context, type, &one));
    REQUIRE(minic_x86_64_core_one_gp_record(&context, one, &bytes));
    REQUIRE(bytes == 4U && !minic_x86_64_core_two_gp_record(&context, one));

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_ARRAY;
    type.has_layout = true;
    type.size = 12U;
    type.alignment = 4U;
    type.value.array.element = integer;
    type.value.array.count = 3U;
    type.value.array.is_complete = true;
    REQUIRE(put(&context, type, &array));
    /* Sealed zero and incomplete extents alone must exclude these record
     * tails from SysV registers, without redundant source-field flags. */
    type.value.array.count = 0U;
    type.size = 0U;
    REQUIRE(put(&context, type, &zero_array));
    type.value.array.is_complete = false;
    type.has_layout = false;
    type.alignment = 0U;
    REQUIRE(put(&context, type, &unsized_array));
    fields[0].type_id = zero_array;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 4U;
    type.value.record.is_complete = true;
    type.value.record.fields = fields;
    type.value.record.field_count = 1U;
    REQUIRE(put(&context, type, &zero_tail));
    REQUIRE(!minic_x86_64_core_one_gp_record(&context, zero_tail, &bytes));
    REQUIRE(!minic_x86_64_core_two_gp_record(&context, zero_tail));
    fields[0].type_id = unsized_array;
    REQUIRE(put(&context, type, &unsized_tail));
    REQUIRE(!minic_x86_64_core_one_gp_record(&context, unsized_tail, &bytes));
    REQUIRE(!minic_x86_64_core_two_gp_record(&context, unsized_tail));
    fields[0].type_id = array;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = 12U;
    type.alignment = 4U;
    type.value.record.is_complete = true;
    type.value.record.fields = fields;
    type.value.record.field_count = 1U;
    REQUIRE(put(&context, type, &two));
    REQUIRE(minic_x86_64_core_two_gp_record(&context, two));

    fields[0].type_id = integer;
    fields[0].byte_offset = 1U;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 1U;
    type.value.record.is_complete = true;
    type.value.record.has_nondefault_layout_policy = true;
    type.value.record.fields = fields;
    type.value.record.field_count = 1U;
    REQUIRE(put(&context, type, &unaligned));
    REQUIRE(!minic_x86_64_core_one_gp_record(&context, unaligned, &bytes));
    REQUIRE(!minic_x86_64_core_two_gp_record(&context, unaligned));
    REQUIRE(!minic_x86_64_core_memory_result_record(&context, unaligned, &bytes));

    fields[0].byte_offset = 0U;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = 24U;
    type.alignment = 8U;
    type.value.record.is_complete = true;
    type.value.record.fields = fields;
    type.value.record.field_count = 1U;
    REQUIRE(put(&context, type, &large));
    REQUIRE(minic_x86_64_core_memory_result_record(&context, large, &bytes));
    REQUIRE(bytes == 24U);
    REQUIRE(!minic_x86_64_core_one_gp_record(&context, large, &bytes));
    REQUIRE(!minic_x86_64_core_two_gp_record(&context, large));

    type.size = 4U;
    type.alignment = 4U;
    REQUIRE(put(&context, type, &small));
    REQUIRE(small != one);
    REQUIRE(minic_x86_64_core_record_abi_compatible(&context, one, one));
    REQUIRE(!minic_x86_64_core_record_abi_compatible(&context, one, small));

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_QUALIFIED;
    type.has_layout = true;
    type.has_abi_alignment_override = true;
    type.size = 4U;
    type.alignment = 16U;
    type.value.qualified.base = one;
    REQUIRE(put(&context, type, &aligned_alias));
    REQUIRE(!minic_x86_64_core_one_gp_record(&context, aligned_alias, &bytes));
    REQUIRE(!minic_x86_64_core_record_abi_compatible(
        &context, one, aligned_alias));
    type.size = 24U;
    type.alignment = 32U;
    type.value.qualified.base = large;
    REQUIRE(put(&context, type, &oversized_alias));
    REQUIRE(!minic_x86_64_core_memory_result_record(
        &context, oversized_alias, &bytes));
    REQUIRE(!minic_x86_64_core_record_abi_compatible(
        &context, MINIC_CORE_TYPE_INVALID, one));
    REQUIRE(!minic_x86_64_core_scalar_width(
        &context, MINIC_CORE_TYPE_INVALID, &bytes, &sign));

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    REQUIRE(put(&context, type, &incomplete));
    REQUIRE(!minic_x86_64_core_record_abi_compatible(
        &context, incomplete, incomplete));
    REQUIRE(!minic_x86_64_core_one_gp_record(&context, incomplete, &bytes));

    minic_core_type_context_destroy(&context);
    (void)puts("X86_CORE_ABI_SHAPE=PASS scalar pointer one_gp two_gp unaligned_reject sret nominal");
    return 0;
}
