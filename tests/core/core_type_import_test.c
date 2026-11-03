#include "core/core_type_import.h"
#include "target/data_layout_source.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "CORE_TYPE_IMPORT=FAIL line=%d expression=%s\n", \
                      __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void) {
    MinicC0Program program;
    MinicCoreTypeContext context;
    MinicCoreTypeImporter importer;
    const MinicDataLayout *layout = minic_x86_64_linux_data_layout();
    MinicRecordId node_record, twin_record, qualified_record;
    MinicType node_source, twin_source, aligned_node_source;
    MinicType pointer_source, pointer_const_source;
    MinicType qualified_record_source, qualified_record_cv, qualified_record_pointer;
    MinicType integer_pointer_source;
    MinicType array_source, function_source, vector_source, const_integer;
    MinicType volatile_integer, const_volatile_integer;
    MinicType parameters[2];
    MinicCoreTypeId node_id, twin_id, aligned_node_id;
    MinicCoreTypeId qualified_record_id, qualified_record_pointer_id;
    MinicCoreTypeId pointer_id, pointer_const_id;
    MinicCoreTypeId int_id, const_int_id, volatile_int_id, const_volatile_int_id;
    MinicCoreTypeId integer_pointer_id;
    MinicCoreTypeId array_id, function_id, repeated_function_id, vector_id, bad_id;
    MinicCoreTypeId register_vector_id;
    MinicCoreType register_vector;
    const MinicCoreType *node, *pointer, *array, *function, *vector, *int_type;
    size_t node_size, node_alignment, field_offset, bit_offset;
    size_t previous_count;

    CHECK(layout != NULL);
    minic_c0_program_initialize(&program);
    minic_core_type_context_initialize(&context);
    CHECK(minic_c0_program_add_record(&program, "Node", 4U, &node_record));
    node_source = minic_type_record(node_record);
    aligned_node_source = node_source;
    aligned_node_source.explicit_alignment = 32U;
    CHECK(minic_type_pointer_to(node_source, &pointer_source));
    CHECK(minic_c0_record_add_field(&program, node_record, "next", 4U,
                                    pointer_source, 1U));
    /* The record builder now materializes an ArrayType for fixed extents. */
    CHECK(minic_c0_record_add_field(&program, node_record, "data", 4U,
                                    minic_type_int(), 3U));
    CHECK(minic_c0_record_add_bit_field(&program, node_record, "flags", 5U,
                                        minic_type_unsigned_int(), 5U));
    CHECK(minic_c0_program_finish_record(&program, node_record));

    /* Same fields, different C declaration => different nominal Core TypeIds. */
    CHECK(minic_c0_program_add_record(&program, "Twin", 4U, &twin_record));
    twin_source = minic_type_record(twin_record);
    CHECK(minic_c0_record_add_field(&program, twin_record, "next", 4U,
                                    pointer_source, 1U));
    CHECK(minic_c0_record_add_field(&program, twin_record, "data", 4U,
                                    minic_type_int(), 3U));
    CHECK(minic_c0_record_add_bit_field(&program, twin_record, "flags", 5U,
                                        minic_type_unsigned_int(), 5U));
    CHECK(minic_c0_program_finish_record(&program, twin_record));
    CHECK(minic_c0_program_add_record(&program, "NodeCV", 6U, &qualified_record));
    qualified_record_source = minic_type_record(qualified_record);
    CHECK(minic_type_add_const(qualified_record_source, &qualified_record_cv));
    CHECK(minic_type_add_volatile(qualified_record_cv, &qualified_record_cv));
    CHECK(minic_type_pointer_to(qualified_record_cv, &qualified_record_pointer));
    CHECK(minic_c0_record_add_field(&program, qualified_record, "next", 4U,
                                    qualified_record_pointer, 1U));
    CHECK(minic_c0_program_finish_record(&program, qualified_record));
    CHECK(minic_c0_program_add_array_type(&program, minic_type_int(), 5U,
                                          &array_source));

    parameters[0] = pointer_source;
    parameters[1] = minic_type_int();
    CHECK(minic_c0_program_add_variadic_function_type(&program,
                minic_type_unsigned_long(), parameters, 2U, true, &function_source));
    CHECK(minic_type_gnu_fixed_vector(minic_type_unsigned_char(), 16U,
                                       &vector_source));
    CHECK(minic_type_add_const(minic_type_int(), &const_integer));
    CHECK(minic_type_add_volatile(minic_type_int(), &volatile_integer));
    CHECK(minic_type_add_volatile(const_integer, &const_volatile_integer));
    CHECK(minic_type_pointer_to(const_integer, &pointer_const_source));
    CHECK(minic_type_pointer_to(minic_type_int(), &integer_pointer_source));

    CHECK(minic_core_type_importer_initialize(&importer, &program, layout, &context));
    CHECK(minic_core_type_import(&importer, node_source, &node_id));
    CHECK(minic_core_type_import(&importer, aligned_node_source, &aligned_node_id));
    CHECK(aligned_node_id != node_id);
    CHECK(minic_core_type_unqualified(&context, aligned_node_id) == node_id);
    CHECK(minic_core_type_get(&context, aligned_node_id)->kind == MINIC_CORE_TYPE_QUALIFIED);
    CHECK(minic_core_type_get(&context, aligned_node_id)->is_layout_override);
    CHECK(minic_core_type_get(&context, aligned_node_id)->alignment == 32U);
    CHECK(minic_core_type_import(&importer, pointer_source, &pointer_id));
    CHECK(minic_core_type_import(&importer, twin_source, &twin_id));
    CHECK(node_id != twin_id);
    CHECK(minic_core_type_import(&importer, qualified_record_source,
                                  &qualified_record_id));
    CHECK(minic_core_type_import(&importer, qualified_record_pointer,
                                  &qualified_record_pointer_id));
    {
        const MinicCoreType *qualified_record_type =
            minic_core_type_get(&context, qualified_record_id);
        const MinicCoreType *recursive_pointer =
            minic_core_type_get(&context, qualified_record_pointer_id);
        const MinicCoreType *recursive_pointee;
        CHECK(qualified_record_type != NULL && recursive_pointer != NULL);
        CHECK(qualified_record_type->kind == MINIC_CORE_TYPE_RECORD &&
              qualified_record_type->value.record.is_complete &&
              qualified_record_type->has_layout);
        CHECK(recursive_pointer->kind == MINIC_CORE_TYPE_POINTER);
        recursive_pointee = minic_core_type_get(&context,
                                               recursive_pointer->value.pointer.pointee);
        CHECK(recursive_pointee != NULL &&
              recursive_pointee->kind == MINIC_CORE_TYPE_QUALIFIED);
        CHECK(recursive_pointee->is_volatile && recursive_pointee->is_read_only);
        CHECK(recursive_pointee->has_layout &&
              recursive_pointee->size == qualified_record_type->size &&
              recursive_pointee->alignment == qualified_record_type->alignment);
        CHECK(minic_core_type_unqualified(&context,
                recursive_pointer->value.pointer.pointee) == qualified_record_id);
        CHECK(qualified_record_type->value.record.fields[0].type_id ==
              qualified_record_pointer_id);
    }
    CHECK(minic_core_type_import(&importer, minic_type_int(), &int_id));
    CHECK(minic_core_type_import(&importer, const_integer, &const_int_id));
    CHECK(int_id != const_int_id); /* CV semantics must survive the C ingress. */
    CHECK(minic_core_type_get(&context, const_int_id)->kind == MINIC_CORE_TYPE_QUALIFIED);
    CHECK(minic_core_type_get(&context, const_int_id)->is_read_only);
    CHECK(!minic_core_type_get(&context, const_int_id)->is_volatile);
    CHECK(minic_core_type_unqualified(&context, const_int_id) == int_id);
    CHECK(minic_core_type_import(&importer, volatile_integer, &volatile_int_id));
    CHECK(minic_core_type_get(&context, volatile_int_id)->is_volatile);
    CHECK(!minic_core_type_get(&context, volatile_int_id)->is_read_only);
    CHECK(minic_core_type_unqualified(&context, volatile_int_id) == int_id);
    CHECK(minic_core_type_import(&importer, const_volatile_integer, &const_volatile_int_id));
    CHECK(const_volatile_int_id != const_int_id &&
          const_volatile_int_id != volatile_int_id);
    CHECK(minic_core_type_get(&context, const_volatile_int_id)->is_volatile &&
          minic_core_type_get(&context, const_volatile_int_id)->is_read_only);
    CHECK(minic_core_type_unqualified(&context, const_volatile_int_id) == int_id);
    CHECK(minic_core_type_import(&importer, integer_pointer_source, &integer_pointer_id));
    CHECK(minic_core_type_import(&importer, pointer_const_source, &pointer_const_id));
    CHECK(pointer_const_id != integer_pointer_id);
    CHECK(minic_core_type_get(&context, pointer_const_id)->value.pointer.pointee == const_int_id);

    CHECK(minic_data_layout_type(layout, &program, node_source,
                                  &node_size, &node_alignment));
    node = minic_core_type_get(&context, node_id);
    pointer = minic_core_type_get(&context, pointer_id);
    CHECK(node != NULL && pointer != NULL);
    CHECK(node->kind == MINIC_CORE_TYPE_RECORD && node->value.record.is_complete);
    CHECK(node->has_layout && node->size == node_size &&
          node->alignment == node_alignment);
    CHECK(node->value.record.field_count == 3U);
    CHECK(pointer->kind == MINIC_CORE_TYPE_POINTER &&
          pointer->value.pointer.pointee == node_id && pointer->size == 8U);
    CHECK(node->value.record.fields[0].type_id == pointer_id);
    CHECK(minic_data_layout_record_field_layout(layout, &program,
                minic_c0_program_record(&program, node_record), 1U,
                &field_offset, &bit_offset));
    CHECK(node->value.record.fields[1].byte_offset == field_offset);
    CHECK(!node->value.record.fields[1].is_bit_field);
    array = minic_core_type_get(&context, node->value.record.fields[1].type_id);
    CHECK(array != NULL && array->kind == MINIC_CORE_TYPE_ARRAY);
    CHECK(array->value.array.count == 3U && array->value.array.is_complete);
    CHECK(array->value.array.element == int_id && array->size == 12U);
    CHECK(node->value.record.fields[2].is_bit_field &&
          node->value.record.fields[2].bit_width == 5U);
    CHECK(minic_data_layout_record_field_layout(layout, &program,
                minic_c0_program_record(&program, node_record), 2U,
                &field_offset, &bit_offset));
    CHECK(node->value.record.fields[2].byte_offset == field_offset &&
          node->value.record.fields[2].bit_offset == bit_offset);

    CHECK(minic_core_type_import(&importer, array_source, &array_id));
    array = minic_core_type_get(&context, array_id);
    CHECK(array != NULL && array->value.array.count == 5U && array->size == 20U);
    CHECK(minic_core_type_import(&importer, function_source, &function_id));
    CHECK(minic_core_type_import(&importer, function_source, &repeated_function_id));
    CHECK(repeated_function_id == function_id);
    function = minic_core_type_get(&context, function_id);
    CHECK(function != NULL && function->kind == MINIC_CORE_TYPE_FUNCTION);
    CHECK(!function->has_layout && function->value.function.is_variadic);
    CHECK(function->value.function.parameter_count == 2U);
    CHECK(function->value.function.parameters[0] == pointer_id &&
          function->value.function.parameters[1] == int_id);
    int_type = minic_core_type_get(&context, function->value.function.return_type);
    CHECK(int_type != NULL && int_type->kind == MINIC_CORE_TYPE_INTEGER &&
          int_type->value.integer.bit_width == 64U &&
          !int_type->value.integer.is_signed);

    CHECK(minic_core_type_import(&importer, vector_source, &vector_id));
    vector = minic_core_type_get(&context, vector_id);
    CHECK(vector != NULL && vector->kind == MINIC_CORE_TYPE_VECTOR);
    CHECK(vector->value.vector.element_count == 16U && vector->size == 16U);
    CHECK(vector->value.vector.representation == MINIC_CORE_VECTOR_STORAGE_ONLY);
    /* The same element count and byte layout do not imply the same Core
     * value representation. Neither representation depends on a C type name. */
    register_vector = *vector;
    register_vector.value.vector.representation = MINIC_CORE_VECTOR_REGISTER_VALUE;
    CHECK(minic_core_type_register(&context, &register_vector, &register_vector_id));
    CHECK(register_vector_id != vector_id);
    CHECK(minic_core_type_get(&context, register_vector_id)->value.vector.representation ==
          MINIC_CORE_VECTOR_REGISTER_VALUE);
    CHECK(minic_core_type_register(&context, &register_vector, &bad_id));
    CHECK(bad_id == register_vector_id);
    register_vector.value.vector.representation = (MinicCoreVectorRepresentation)5;
    bad_id = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_type_register(&context, &register_vector, &bad_id));
    CHECK(bad_id == MINIC_CORE_TYPE_INVALID);

    /* One failed conversion poisons the transaction, never returns a valid ID. */
    previous_count = context.type_count;
    bad_id = 0U;
    CHECK(!minic_core_type_import(&importer,
                                   minic_type_record(MINIC_RECORD_INVALID), &bad_id));
    CHECK(bad_id == MINIC_CORE_TYPE_INVALID && importer.failed);
    CHECK(context.type_count == previous_count);
    CHECK(!minic_core_type_import(&importer, minic_type_int(), &bad_id));
    CHECK(bad_id == MINIC_CORE_TYPE_INVALID);

    minic_core_type_importer_destroy(&importer);
    minic_core_type_context_destroy(&context);
    minic_c0_program_destroy(&program);
    (void)puts("CORE_TYPE_IMPORT=PASS recursion nominal layout array_field bitfield function vector poison");
    return 0;
}
