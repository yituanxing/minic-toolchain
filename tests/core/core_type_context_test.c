#include "core/core_type.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "CORE_TYPE_CONTEXT=FAIL line=%d expression=%s\n", \
                      __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void) {
    MinicCoreTypeContext context;
    MinicCoreType type;
    MinicCoreTypeId void_id, integer_id, duplicate_id;
    MinicCoreTypeId record_id, pointer_id, array_id, incomplete_array_id;
    MinicCoreTypeId fixed_array_id, aligned_array_id;
    MinicCoreTypeId function_id, another_function_id, alternate_function_id, opaque_id;
    MinicCoreTypeId float_id, vector_id;
    MinicCoreRecordField field;
    MinicCoreTypeId params[2];
    const MinicCoreType *stored;
    size_t before;

    minic_core_type_context_initialize(&context);
    CHECK(context.type_count == 0U && context.types == NULL);
    CHECK(minic_core_type_get(&context, MINIC_CORE_TYPE_INVALID) == NULL);
    CHECK(minic_core_type_get(&context, 0U) == NULL);
    CHECK(minic_core_type_get(NULL, 0U) == NULL);
    minic_core_type_context_initialize(NULL);
    minic_core_type_context_destroy(NULL);

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_VOID;
    CHECK(minic_core_type_register(&context, &type, &void_id));
    CHECK(void_id == 0U);
    CHECK(minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(duplicate_id == void_id && context.type_count == 1U);

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    CHECK(minic_core_type_register(&context, &type, &integer_id));
    CHECK(minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(duplicate_id == integer_id && context.type_count == 2U);
    type.value.integer.is_signed = false;
    CHECK(minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(duplicate_id != integer_id);
    type.value.integer.is_signed = true;

    /* A malformed type cannot publish a half-constructed TypeId. */
    before = context.type_count;
    type.alignment = 0U;
    duplicate_id = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(context.type_count == before);
    CHECK(duplicate_id == MINIC_CORE_TYPE_INVALID);

    /* A recursive record has a nominal identity before its body exists. */
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    CHECK(minic_core_type_register(&context, &type, &record_id));
    CHECK(minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(duplicate_id != record_id);

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = record_id;
    before = context.type_count;
    type.value.pointer.pointee = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(context.type_count == before);
    type.value.pointer.pointee = record_id;
    CHECK(minic_core_type_register(&context, &type, &pointer_id));
    CHECK(minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(duplicate_id == pointer_id);

    (void)memset(&field, 0, sizeof(field));
    field.type_id = pointer_id;
    field.byte_offset = 8U;
    field.alignment = 8U;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = 16U;
    type.alignment = 8U;
    type.value.record.is_complete = true;
    type.value.record.fields = &field;
    type.value.record.field_count = 1U;
    before = context.type_count;
    type.value.record.field_count = SIZE_MAX;
    CHECK(!minic_core_type_complete_record(&context, record_id, &type));
    CHECK(!minic_core_type_get(&context, record_id)->value.record.is_complete);
    type.value.record.field_count = 1U;
    field.type_id = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_type_complete_record(&context, record_id, &type));
    CHECK(!minic_core_type_get(&context, record_id)->value.record.is_complete);
    CHECK(context.type_count == before);
    field.type_id = pointer_id;
    CHECK(minic_core_type_complete_record(&context, record_id, &type));
    field.byte_offset = 99U;
    stored = minic_core_type_get(&context, record_id);
    CHECK(stored != NULL && stored->value.record.is_complete);
    CHECK(stored->value.record.field_count == 1U);
    CHECK(stored->value.record.fields[0].type_id == pointer_id);
    CHECK(stored->value.record.fields[0].byte_offset == 8U);
    CHECK(!minic_core_type_complete_record(&context, record_id, &type));
    CHECK(minic_core_type_get(&context, pointer_id)->value.pointer.pointee == record_id);

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_ARRAY;
    type.has_layout = true;
    type.size = 0U;
    type.alignment = 4U;
    type.value.array.element = integer_id;
    type.value.array.is_complete = true;
    before = context.type_count;
    type.value.array.element = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(context.type_count == before);
    type.value.array.element = integer_id;
    CHECK(minic_core_type_register(&context, &type, &array_id));
    type.has_layout = false;
    type.alignment = 0U;
    type.value.array.is_complete = false;
    CHECK(minic_core_type_register(&context, &type, &incomplete_array_id));
    CHECK(incomplete_array_id != array_id);

    /* Array storage is one canonical CoreType: no second object-level count.
     * Registration must reject mismatched/overflowing extents atomically. */
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_ARRAY;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 4U;
    type.value.array.element = integer_id;
    type.value.array.count = 2U;
    type.value.array.is_complete = true;
    CHECK(minic_core_type_register(&context, &type, &fixed_array_id));
    CHECK(minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(duplicate_id == fixed_array_id);
    before = context.type_count;
    type.size = 4U;
    CHECK(!minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(context.type_count == before);
    type.size = 8U;
    type.alignment = 8U;
    CHECK(!minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(context.type_count == before);
    type.has_abi_alignment_override = true;
    CHECK(minic_core_type_register(&context, &type, &aligned_array_id));
    CHECK(aligned_array_id != fixed_array_id);
    before = context.type_count;
    type.has_abi_alignment_override = false;
    type.alignment = 4U;
    type.value.array.count = SIZE_MAX;
    type.size = SIZE_MAX;
    CHECK(!minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(context.type_count == before);
    type.value.array.count = 0U;
    type.size = 0U;
    CHECK(minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(duplicate_id == array_id);
    CHECK(context.type_count == before);
    type.has_layout = false;
    CHECK(!minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(context.type_count == before);
    type.value.array.is_complete = false;
    type.alignment = 0U;
    CHECK(minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(duplicate_id == incomplete_array_id);
    type.has_layout = true;
    type.alignment = 4U;
    CHECK(!minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(context.type_count == before);
    type.has_layout = false;
    type.alignment = 0U;
    type.value.array.count = 1U;
    CHECK(!minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(context.type_count == before);

    params[0] = integer_id;
    params[1] = pointer_id;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_FUNCTION;
    type.value.function.return_type = integer_id;
    type.value.function.parameters = params;
    type.value.function.parameter_count = 2U;
    type.value.function.is_variadic = true;
    type.value.function.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    before = context.type_count;
    type.value.function.parameter_count = SIZE_MAX;
    CHECK(!minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(context.type_count == before);
    type.value.function.parameter_count = 2U;
    type.value.function.return_type = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(context.type_count == before);
    type.value.function.return_type = integer_id;
    params[1] = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(context.type_count == before);
    params[1] = pointer_id;
    CHECK(minic_core_type_register(&context, &type, &function_id));
    params[1] = void_id;
    stored = minic_core_type_get(&context, function_id);
    CHECK(stored != NULL && stored->value.function.parameters[1] == pointer_id);
    params[1] = pointer_id;
    CHECK(minic_core_type_register(&context, &type, &another_function_id));
    CHECK(another_function_id != function_id); /* Function ID is nominal. */
    type.value.function.calling_convention = MINIC_CALLING_CONVENTION_MS_X64;
    CHECK(minic_core_type_register(&context, &type, &alternate_function_id));
    CHECK(alternate_function_id != function_id);

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.floating.format = MINIC_CORE_FLOAT_BINARY32;
    CHECK(minic_core_type_register(&context, &type, &float_id));
    CHECK(minic_core_type_get(&context, float_id)->value.floating.format ==
          MINIC_CORE_FLOAT_BINARY32);

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_VECTOR;
    type.has_layout = true;
    type.size = 16U;
    type.alignment = 16U;
    type.value.vector.element = integer_id;
    type.value.vector.element_count = 4U;
    before = context.type_count;
    type.value.vector.element = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(context.type_count == before);
    type.value.vector.element = integer_id;
    CHECK(minic_core_type_register(&context, &type, &vector_id));
    CHECK(minic_core_type_get(&context, vector_id)->value.vector.element_count == 4U);

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_OPAQUE;
    CHECK(minic_core_type_register(&context, &type, &opaque_id));
    CHECK(minic_core_type_register(&context, &type, &duplicate_id));
    CHECK(duplicate_id != opaque_id);

    minic_core_type_context_destroy(&context);
    CHECK(context.types == NULL && context.type_count == 0U);
    (void)puts("CORE_TYPE_CONTEXT=PASS scalar pointer recursive_record fixed_zero_incomplete_array function_abi ownership rollback");
    return 0;
}
