#include "core/core_type.h"
#include "target/riscv64/abi.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c) do { \
    if (!(c)) { \
        (void)fprintf(stderr, "RV64_CORETYPE_ABI=FAIL line=%d expr=%s\n", \
                      __LINE__, #c); \
        return 1; \
    } \
} while (0)

static bool register_integer(MinicCoreTypeContext *types, unsigned int bits,
                             MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = bits / 8U;
    type.alignment = bits == 128U ? 16U : type.size;
    type.value.integer.bit_width = bits;
    type.value.integer.is_signed = true;
    return minic_core_type_register(types, &type, id);
}

static bool register_float(MinicCoreTypeContext *types,
                           MinicCoreFloatFormat format, size_t size,
                           MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = size;
    type.alignment = size;
    type.value.floating.format = format;
    return minic_core_type_register(types, &type, id);
}

static bool register_record(MinicCoreTypeContext *types, MinicCoreTypeId field_type,
                            size_t size, bool is_union, MinicCoreTypeId *id) {
    MinicCoreRecordField field = {0};
    MinicCoreType type = {0};
    field.type_id = field_type;
    field.alignment = 8U;
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = size;
    type.alignment = 8U;
    type.value.record.is_complete = true;
    type.value.record.is_union = is_union;
    type.value.record.fields = &field;
    type.value.record.field_count = 1U;
    return minic_core_type_register(types, &type, id);
}

int main(void) {
    MinicCoreTypeContext types;
    MinicCoreTypeId i32, i64, i128, f64, ext, fp_record, fp_union, big_record;
    MinicRiscv64AbiValue value;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiArgumentLocation location;
    MinicCoreType type = {0};
    MinicCoreTypeId pointer;

    minic_core_type_context_initialize(&types);
    CHECK(register_integer(&types, 32U, &i32));
    CHECK(register_integer(&types, 64U, &i64));
    CHECK(register_integer(&types, 128U, &i128));
    CHECK(register_float(&types, MINIC_CORE_FLOAT_BINARY64, 8U, &f64));
    CHECK(register_float(&types, MINIC_CORE_FLOAT_BINARY128, 16U, &ext));
    CHECK(register_record(&types, f64, 16U, false, &fp_record));
    CHECK(register_record(&types, f64, 16U, true, &fp_union));
    CHECK(register_record(&types, i64, 24U, false, &big_record));

    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = big_record;
    CHECK(minic_core_type_register(&types, &type, &pointer));

    CHECK(minic_riscv64_abi_classify_core_value_profile(&types, i32, false, &value));
    CHECK(value.kind == MINIC_RISCV64_ABI_VALUE_INTEGER &&
          value.storage_size == 4U && value.slot_count == 1U);
    CHECK(minic_riscv64_abi_classify_core_value_profile(&types, pointer, false, &value));
    CHECK(value.kind == MINIC_RISCV64_ABI_VALUE_INTEGER);
    CHECK(minic_riscv64_abi_classify_core_value_profile(&types, f64, false, &value));
    CHECK(value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT && value.slot_count == 1U);
    CHECK(minic_riscv64_abi_classify_core_value_profile(&types, i128, false, &value));
    CHECK(value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR && value.slot_count == 2U);
    CHECK(minic_riscv64_abi_classify_core_value_profile(&types, ext, true, &value));
    CHECK(value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR);

    /* ABI reject: real FP struct needs unimplemented FPR flattening. A
     * union containing the same FP member takes integer convention. */
    CHECK(!minic_riscv64_abi_classify_core_value_profile(
        &types, fp_record, false, &value));
    CHECK(minic_riscv64_abi_classify_core_value_profile(
        &types, fp_record, true, &value));
    CHECK(value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE && value.slot_count == 2U);
    CHECK(minic_riscv64_abi_classify_core_value_profile(
        &types, fp_union, false, &value));
    CHECK(value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE && value.slot_count == 2U);
    CHECK(minic_riscv64_abi_classify_core_value_profile(
        &types, big_record, false, &value));
    CHECK(value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT && value.storage_size == 24U);
    CHECK(!minic_riscv64_abi_classify_core_value_profile(
        &types, MINIC_CORE_TYPE_INVALID, false, &value));

    CHECK(minic_riscv64_abi_cursor_initialize_for_core_return_profile(
        &types, big_record, false, &cursor, &value));
    CHECK(cursor.integer_register_count == 1U &&
          value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT);
    CHECK(minic_riscv64_abi_place_core_argument(
        &types, i64, true, &cursor, &location));
    CHECK(location.integer_register_begin == 1U &&
          location.integer_register_count == 1U);

    minic_riscv64_abi_cursor_initialize(&cursor);
    cursor.integer_register_count = 1U;
    CHECK(minic_riscv64_abi_place_core_argument(
        &types, i128, false, &cursor, &location));
    CHECK(location.integer_register_begin == 2U &&
          location.integer_register_count == 2U &&
          cursor.integer_register_count == 4U);
    minic_riscv64_abi_cursor_initialize(&cursor);
    CHECK(minic_riscv64_abi_place_core_argument(
        &types, f64, true, &cursor, &location));
    CHECK(location.floating_register_begin == 0U &&
          location.floating_register_count == 1U);
    minic_riscv64_abi_cursor_initialize(&cursor);
    cursor.lp64_soft_float = true;
    CHECK(minic_riscv64_abi_place_core_argument(
        &types, f64, true, &cursor, &location));
    CHECK(location.integer_register_count == 1U &&
          location.floating_register_count == 0U);

    /* Invalid contexts/types never fall back to C DataLayout. */
    CHECK(!minic_riscv64_abi_place_core_argument(
        &types, MINIC_CORE_TYPE_INVALID, true, &cursor, &location));
    minic_core_type_context_destroy(&types);
    (void)puts("RV64_CORETYPE_ABI=PASS LP64 LP64D union_fp_reject wide_pair hidden_result");
    return 0;
}
