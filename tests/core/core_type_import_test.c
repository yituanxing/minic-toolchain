#include "compiler/c_lower/core_type_import.h"
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

/* C's four TS 18661 spellings are source identities, not Core floating
 * encodings. Every target must import its physical representation exactly,
 * while same-layout C aliases can share the execution type safely. */
static bool check_physical_float_import(void) {
    static const MinicTargetArchitecture architectures[] = {
        MINIC_TARGET_ARCH_RISCV32, MINIC_TARGET_ARCH_RISCV64,
        MINIC_TARGET_ARCH_X86_64, MINIC_TARGET_ARCH_AARCH64,
        MINIC_TARGET_ARCH_LOONGARCH64,
    };
    size_t i;
    for (i = 0U; i < sizeof(architectures) / sizeof(architectures[0]); ++i) {
        MinicTargetInvocation target;
        MinicC0Program program;
        MinicCoreTypeContext types;
        MinicCoreTypeImporter importer;
        MinicCoreTypeId f32, f64, ld = MINIC_CORE_TYPE_INVALID;
        MinicCoreTypeId f32_alias, f64_alias, f32x, f64x;
        const MinicCoreType *wide;
        bool ok;
        if (!minic_target_invocation_begin(
                &target, minic_target_info_for_architecture(architectures[i])))
            return false;
        minic_c0_program_initialize(&program);
        minic_core_type_context_initialize(&types);
        if (!minic_core_type_importer_initialize(&importer, &program, &target, &types)) {
            minic_core_type_context_destroy(&types);
            minic_c0_program_destroy(&program);
            return false;
        }
        ok = minic_core_type_import(&importer, minic_type_float(), &f32) &&
             minic_core_type_import(&importer, minic_type_double(), &f64) &&
             minic_core_type_import(&importer, minic_type_long_double(), &ld);
        if (ok && architectures[i] != MINIC_TARGET_ARCH_RISCV32) {
            ok = minic_core_type_import(&importer, minic_type_float32(), &f32_alias) &&
                 minic_core_type_import(&importer, minic_type_float64(), &f64_alias) &&
                 minic_core_type_import(&importer, minic_type_float32x(), &f32x) &&
                 minic_core_type_import(&importer, minic_type_float64x(), &f64x) &&
                 f32 == f32_alias && f64 == f64_alias &&
                 f64 == f32x && ld == f64x;
        }
        wide = minic_core_type_get(&types, ld);
        ok = ok && wide != NULL && wide->size == 16U &&
             wide->value.floating.format ==
                (architectures[i] == MINIC_TARGET_ARCH_X86_64
                     ? MINIC_CORE_FLOAT_X87_EXTENDED80
                     : MINIC_CORE_FLOAT_BINARY128);
        if (ok && (architectures[i] == MINIC_TARGET_ARCH_RISCV64 ||
                   architectures[i] == MINIC_TARGET_ARCH_X86_64)) {
            MinicCoreTypeId ieee128;
            ok = minic_core_type_import(&importer, minic_type_float128(), &ieee128) &&
                 (ieee128 == ld) == (architectures[i] == MINIC_TARGET_ARCH_RISCV64);
        }
        minic_core_type_importer_destroy(&importer);
        minic_core_type_context_destroy(&types);
        minic_c0_program_destroy(&program);
        if (!ok) return false;
    }
    return true;
}

int main(void) {
    MinicC0Program program;
    MinicCoreTypeContext context;
    MinicCoreTypeImporter importer;
    MinicTargetInvocation invocation;
    const MinicTargetInvocation *source = &invocation;
    MinicRecordId node_record, twin_record, qualified_record;
    MinicRecordId packed_record, flexible_record, zero_record;
    MinicEnumId incomplete_enum_a, incomplete_enum_b, complete_enum;
    MinicType node_source, twin_source, aligned_node_source;
    MinicType pointer_source, pointer_const_source;
    MinicType qualified_record_source, qualified_record_cv, qualified_record_pointer;
    MinicType integer_pointer_source;
    MinicType array_source, function_source, vector_source, const_integer;
    MinicType volatile_integer, const_volatile_integer;
    MinicType incomplete_enum_a_type, incomplete_enum_b_type;
    MinicType incomplete_enum_pointer, complete_enum_type;
    MinicType packed_source, flexible_source, zero_source;
    MinicType flexible_array_source, zero_array_source;
    MinicType parameters[2];
    MinicCoreTypeId node_id, twin_id, aligned_node_id;
    MinicCoreTypeId qualified_record_id, qualified_record_pointer_id;
    MinicCoreTypeId pointer_id, pointer_const_id;
    MinicCoreTypeId int_id, const_int_id, volatile_int_id, const_volatile_int_id;
    MinicCoreTypeId integer_pointer_id;
    MinicCoreTypeId array_id, function_id, repeated_function_id, vector_id, bad_id;
    MinicCoreTypeId register_vector_id;
    MinicCoreTypeId incomplete_enum_a_id, incomplete_enum_b_id;
    MinicCoreTypeId incomplete_enum_pointer_id, repeated_incomplete_enum_a_id;
    MinicCoreTypeId complete_enum_id;
    MinicCoreTypeId packed_id, flexible_id, zero_id;
    MinicCoreType register_vector;
    const MinicCoreType *node, *pointer, *array, *function, *vector, *int_type;
    size_t node_size, node_alignment, field_offset, bit_offset, storage_alignment;
    size_t previous_count;

    CHECK(check_physical_float_import());

    CHECK(minic_target_invocation_begin(
        &invocation, minic_target_info_for_architecture(MINIC_TARGET_ARCH_X86_64)));
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
    /* C policy stops at ingress: Core captures only neutral layout facts
     * and canonical materialized array extents, not declarator spellings. */
    CHECK(minic_c0_program_add_record(&program, "Packed", 6U, &packed_record));
    packed_source = minic_type_record(packed_record);
    CHECK(minic_c0_record_add_field(&program, packed_record, "value", 5U,
                                    minic_type_int(), 1U));
    program.records[packed_record].is_packed = true;
    program.records[packed_record].fields[0].is_packed = true;
    CHECK(minic_c0_record_add_bit_field(&program, packed_record, "flags", 5U,
                                        minic_type_unsigned_int(), 3U));
    CHECK(minic_c0_record_add_unnamed_bit_field(&program, packed_record,
                                               minic_type_unsigned_int(), 0U));
    CHECK(minic_c0_record_add_field(&program, packed_record, "tail", 4U,
                                    minic_type_signed_char(), 1U));
    CHECK(minic_c0_program_finish_record(&program, packed_record));

    CHECK(minic_c0_program_add_incomplete_array_type(
        &program, minic_type_int(), &flexible_array_source));
    CHECK(minic_c0_program_add_record(&program, "Flexible", 8U, &flexible_record));
    flexible_source = minic_type_record(flexible_record);
    CHECK(minic_c0_record_add_field(&program, flexible_record, "head", 4U,
                                    minic_type_int(), 1U));
    CHECK(minic_c0_record_add_field(&program, flexible_record, "tail", 4U,
                                    flexible_array_source, 1U));
    program.records[flexible_record].fields[1].is_flexible_array = true;
    CHECK(minic_c0_program_finish_record(&program, flexible_record));

    CHECK(minic_c0_program_add_zero_length_array_type(
        &program, minic_type_int(), &zero_array_source));
    CHECK(minic_c0_program_add_record(&program, "ZeroTail", 8U, &zero_record));
    zero_source = minic_type_record(zero_record);
    CHECK(minic_c0_record_add_field(&program, zero_record, "head", 4U,
                                    minic_type_int(), 1U));
    CHECK(minic_c0_record_add_field(&program, zero_record, "tail", 4U,
                                    zero_array_source, 1U));
    CHECK(minic_c0_program_finish_record(&program, zero_record));
    CHECK(minic_c0_program_add_array_type(&program, minic_type_int(), 5U,
                                          &array_source));

    /* GNU permits forward enums in declarations of callback signatures.
     * They carry nominal identity, but no integer ABI or object storage until
     * completed. An unrelated complete enum still imports as its integer ABI. */
    CHECK(minic_c0_program_add_enum(&program, "OpaqueA", 7U, &incomplete_enum_a));
    CHECK(minic_c0_program_add_enum(&program, "OpaqueB", 7U, &incomplete_enum_b));
    CHECK(minic_c0_program_add_enum(&program, "Complete", 8U, &complete_enum));
    CHECK(minic_c0_program_finish_enum(&program, complete_enum,
                                       minic_type_unsigned_int()));
    incomplete_enum_a_type = minic_type_enum(incomplete_enum_a);
    incomplete_enum_b_type = minic_type_enum(incomplete_enum_b);
    complete_enum_type = minic_type_enum(complete_enum);
    CHECK(minic_type_pointer_to(incomplete_enum_a_type, &incomplete_enum_pointer));

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

    CHECK(minic_core_type_importer_initialize(&importer, &program, source, &context));
    CHECK(minic_core_type_import(&importer, node_source, &node_id));
    CHECK(minic_core_type_import(&importer, aligned_node_source, &aligned_node_id));
    CHECK(aligned_node_id != node_id);
    CHECK(minic_core_type_unqualified(&context, aligned_node_id) == node_id);
    CHECK(minic_core_type_get(&context, aligned_node_id)->kind == MINIC_CORE_TYPE_ALIGNED);
    CHECK(minic_core_type_get(&context, aligned_node_id)->has_abi_alignment_override);
    CHECK(minic_core_type_get(&context, aligned_node_id)->alignment == 32U);
    CHECK(minic_core_type_import(&importer, pointer_source, &pointer_id));
    CHECK(minic_core_type_import(&importer, packed_source, &packed_id));
    CHECK(minic_core_type_get(&context, packed_id)->value.record.has_nondefault_layout_policy);
    CHECK(minic_core_type_get(&context, packed_id)->value.record.fields[0]
              .has_nondefault_layout_policy);
    {
        const MinicCoreType *packed = minic_core_type_get(&context, packed_id);
        const MinicRecord *source_record = minic_c0_program_record(&program, packed_record);
        CHECK(packed != NULL && packed->value.record.field_count == 4U);
        CHECK(packed->value.record.fields[1].is_bit_field &&
              packed->value.record.fields[1].alignment == 1U);
        CHECK(packed->value.record.fields[2].is_bit_field &&
              packed->value.record.fields[2].bit_width == 0U &&
              packed->value.record.fields[2].alignment == 4U);
        CHECK(minic_data_layout_record_field_storage_alignment(
                  source, &program, source_record, 2U, &storage_alignment) &&
              storage_alignment == packed->value.record.fields[2].alignment);
        CHECK(!minic_data_layout_record_field_alignment(
                  source, &program, source_record, 2U, &storage_alignment));
    }
    CHECK(minic_core_type_import(&importer, flexible_source, &flexible_id));
    CHECK(minic_core_type_import(&importer, zero_source, &zero_id));
    {
        const MinicCoreType *flexible = minic_core_type_get(&context, flexible_id);
        const MinicCoreType *zero = minic_core_type_get(&context, zero_id);
        const MinicCoreType *tail;
        CHECK(flexible != NULL && zero != NULL &&
              flexible->value.record.field_count == 2U &&
              zero->value.record.field_count == 2U);
        tail = minic_core_type_get(&context, flexible->value.record.fields[1].type_id);
        CHECK(tail != NULL && tail->kind == MINIC_CORE_TYPE_ARRAY &&
              !tail->value.array.is_complete && tail->value.array.count == 0U &&
              !tail->has_layout);
        tail = minic_core_type_get(&context, zero->value.record.fields[1].type_id);
        CHECK(tail != NULL && tail->kind == MINIC_CORE_TYPE_ARRAY &&
              tail->value.array.is_complete && tail->value.array.count == 0U &&
              tail->has_layout && tail->size == 0U);
    }
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
        CHECK(recursive_pointee == qualified_record_type);
        CHECK(recursive_pointee->has_layout &&
              recursive_pointee->size == qualified_record_type->size &&
              recursive_pointee->alignment == qualified_record_type->alignment);
        CHECK(minic_core_type_unqualified(&context,
                recursive_pointer->value.pointer.pointee) == qualified_record_id);
        CHECK(qualified_record_type->value.record.fields[0].type_id ==
              qualified_record_pointer_id);
    }
    CHECK(minic_core_type_import(&importer, minic_type_int(), &int_id));
    CHECK(minic_core_type_import(&importer, incomplete_enum_a_type, &incomplete_enum_a_id));
    CHECK(minic_core_type_import(&importer, incomplete_enum_b_type, &incomplete_enum_b_id));
    CHECK(minic_core_type_import(&importer, incomplete_enum_a_type,
                                  &repeated_incomplete_enum_a_id));
    CHECK(incomplete_enum_a_id == repeated_incomplete_enum_a_id &&
          incomplete_enum_a_id != incomplete_enum_b_id);
    CHECK(minic_core_type_get(&context, incomplete_enum_a_id)->kind ==
          MINIC_CORE_TYPE_OPAQUE &&
          !minic_core_type_get(&context, incomplete_enum_a_id)->has_layout);
    CHECK(minic_core_type_get(&context, incomplete_enum_b_id)->kind ==
          MINIC_CORE_TYPE_OPAQUE &&
          !minic_core_type_get(&context, incomplete_enum_b_id)->has_layout);
    CHECK(minic_core_type_import(&importer, incomplete_enum_pointer,
                                  &incomplete_enum_pointer_id));
    CHECK(minic_core_type_get(&context, incomplete_enum_pointer_id)->kind ==
          MINIC_CORE_TYPE_POINTER &&
          minic_core_type_get(&context, incomplete_enum_pointer_id)
              ->value.pointer.pointee == incomplete_enum_a_id);
    CHECK(minic_core_type_import(&importer, complete_enum_type, &complete_enum_id));
    CHECK(minic_core_type_get(&context, complete_enum_id)->kind ==
          MINIC_CORE_TYPE_INTEGER &&
          minic_core_type_get(&context, complete_enum_id)->has_layout);
    CHECK(minic_core_type_import(&importer, const_integer, &const_int_id));
    CHECK(int_id == const_int_id); /* cv affects accesses, never execution type identity. */
    CHECK(minic_core_type_unqualified(&context, const_int_id) == int_id);
    CHECK(minic_core_type_import(&importer, volatile_integer, &volatile_int_id));
    CHECK(volatile_int_id == int_id);
    CHECK(minic_core_type_unqualified(&context, volatile_int_id) == int_id);
    CHECK(minic_core_type_import(&importer, const_volatile_integer, &const_volatile_int_id));
    CHECK(const_volatile_int_id == int_id);
    CHECK(minic_core_type_unqualified(&context, const_volatile_int_id) == int_id);
    CHECK(minic_core_type_import(&importer, integer_pointer_source, &integer_pointer_id));
    CHECK(minic_core_type_import(&importer, pointer_const_source, &pointer_const_id));
    CHECK(pointer_const_id == integer_pointer_id);
    CHECK(minic_core_type_get(&context, pointer_const_id)->value.pointer.pointee == int_id);

    CHECK(minic_data_layout_type(source, &program, node_source,
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
    CHECK(minic_data_layout_record_field_layout(source, &program,
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
    CHECK(minic_data_layout_record_field_layout(source, &program,
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
    (void)puts("CORE_TYPE_IMPORT=PASS recursion nominal incomplete_enum_opaque complete_enum_integer layout packed flexible zero_array array_field bitfield function vector poison");
    return 0;
}
