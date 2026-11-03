#include "core/core_type_import.h"
#include "target/data_layout_source.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* A C type is a source descriptor, never a Core identity. All source IDs and
 * target layout queries are restricted to this translation-unit ingress. */
static bool import_type(MinicCoreTypeImporter *importer, MinicType source,
                        MinicCoreTypeId *result, unsigned depth);

bool minic_core_type_importer_initialize(MinicCoreTypeImporter *importer,
                                        const MinicC0Program *program,
                                        const MinicDataLayout *layout,
                                        MinicCoreTypeContext *context) {
    size_t i;
    if (importer == NULL) return false;
    (void)memset(importer, 0, sizeof(*importer));
    if (program == NULL || layout == NULL || context == NULL) {
        importer->failed = true;
        return false;
    }
    importer->program = program;
    importer->layout = layout;
    importer->context = context;
    importer->record_count = program->record_count;
    importer->function_count = program->function_type_count;
    for (i = 0U; i < sizeof(importer->opaque_types) /
                       sizeof(importer->opaque_types[0]); ++i)
        importer->opaque_types[i] = MINIC_CORE_TYPE_INVALID;
    if (program->record_count != 0U) {
        if (program->records == NULL ||
            program->record_count > SIZE_MAX / sizeof(*importer->record_types)) {
            importer->failed = true;
            return false;
        }
        importer->record_types = malloc(program->record_count * sizeof(*importer->record_types));
        if (importer->record_types == NULL) {
            importer->failed = true;
            return false;
        }
        for (i = 0U; i < program->record_count; ++i)
            importer->record_types[i] = MINIC_CORE_TYPE_INVALID;
    }
    if (program->function_type_count != 0U) {
        if (program->function_types == NULL ||
            program->function_type_count > SIZE_MAX / sizeof(*importer->function_types)) {
            importer->failed = true;
            free(importer->record_types);
            importer->record_types = NULL;
            return false;
        }
        importer->function_types = malloc(program->function_type_count *
                                         sizeof(*importer->function_types));
        if (importer->function_types == NULL) {
            importer->failed = true;
            free(importer->record_types);
            importer->record_types = NULL;
            return false;
        }
        for (i = 0U; i < program->function_type_count; ++i)
            importer->function_types[i] = MINIC_CORE_TYPE_INVALID;
    }
    return true;
}

void minic_core_type_importer_destroy(MinicCoreTypeImporter *importer) {
    if (importer == NULL) return;
    free(importer->record_types);
    free(importer->function_types);
    (void)memset(importer, 0, sizeof(*importer));
}

static bool source_layout(MinicCoreTypeImporter *importer, MinicType source,
                          MinicCoreType *type) {
    if (!minic_data_layout_type(importer->layout, importer->program,
                                source, &type->size, &type->alignment))
        return false;
    type->has_layout = true;
    return true;
}

/* The public DataLayout field-alignment query intentionally rejects bit
 * fields; bit-field *placement* remains authoritative in its offset query.
 * Snapshot the effective storage boundary as the same pack/explicit-align
 * rule used by the DataLayout record walker. */
static bool bit_field_storage_alignment(const MinicRecord *record,
                                        const MinicRecordField *field,
                                        size_t natural_alignment,
                                        size_t *result) {
    size_t pack = record->is_packed ? 1U : record->pack_alignment;
    size_t alignment = natural_alignment;
    if (natural_alignment == 0U ||
        (pack != 0U && (pack > 16U || (pack & (pack - 1U)) != 0U)))
        return false;
    if (pack != 0U && alignment > pack) alignment = pack;
    if (field->is_packed) alignment = 1U;
    if (field->explicit_alignment != 0U) {
        if ((field->explicit_alignment & (field->explicit_alignment - 1U)) != 0U)
            return false;
        if (field->explicit_alignment > alignment)
            alignment = field->explicit_alignment;
    }
    if (field->bit_width == 0U && field->name_length == 0U &&
        natural_alignment > alignment)
        alignment = natural_alignment;
    *result = alignment;
    return true;
}

static bool import_array(MinicCoreTypeImporter *importer, MinicType element,
                         size_t count, bool zero_length, bool complete,
                         size_t size, size_t alignment, bool has_layout,
                         MinicCoreTypeId *result, unsigned depth) {
    MinicCoreType type = {0};
    if (!import_type(importer, element, &type.value.array.element, depth + 1U))
        return false;
    type.kind = MINIC_CORE_TYPE_ARRAY;
    type.value.array.count = count;
    type.value.array.is_zero_length = zero_length;
    type.value.array.is_complete = complete;
    type.has_layout = has_layout;
    type.size = size;
    type.alignment = alignment;
    return minic_core_type_register(importer->context, &type, result);
}

static bool import_record(MinicCoreTypeImporter *importer, MinicType source,
                          MinicCoreTypeId *result, unsigned depth) {
    const MinicRecord *record;
    MinicCoreType type = {0};
    MinicCoreRecordField *fields = NULL;
    MinicCoreTypeId id;
    size_t i;
    if (source.record_id >= importer->record_count) return false;
    id = importer->record_types[source.record_id];
    if (id != MINIC_CORE_TYPE_INVALID) {
        *result = id;
        return true;
    }
    record = minic_c0_program_record(importer->program, source.record_id);
    if (record == NULL) return false;
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.value.record.is_union = record->is_union;
    type.value.record.is_packed = record->is_packed;
    type.value.record.pack_alignment = record->pack_alignment;
    type.value.record.explicit_alignment = record->explicit_alignment;
    type.value.record.is_transparent_union = record->is_transparent_union;
    /* The C record already has a complete DataLayout even though Core's
     * recursive member graph must be populated in two phases. Seed that
     * layout before publishing provisional identity so pointer-to-const
     * self references receive the final size/alignment in their wrapper. */
    if (record->is_complete && !source_layout(importer, source, &type))
        return false;
    /* The provisional identity is visible before importing any member. This
     * permits mutually recursive records through pointers. */
    if (!minic_core_type_register(importer->context, &type, &id)) return false;
    importer->record_types[source.record_id] = id;
    if (!record->is_complete) {
        *result = id;
        return true;
    }
    if (record->field_count != 0U) {
        if (record->fields == NULL ||
            record->field_count > SIZE_MAX / sizeof(*fields)) return false;
        fields = calloc(record->field_count, sizeof(*fields));
        if (fields == NULL) return false;
    }
    type.value.record.fields = fields;
    type.value.record.field_count = record->field_count;
    for (i = 0U; i < record->field_count; ++i) {
        const MinicRecordField *field = &record->fields[i];
        MinicCoreTypeId field_type;
        MinicArrayObjectInfo array_info;
        bool field_array;
        MinicType field_layout_type;
        size_t field_size, field_alignment;
        field_array = minic_c0_record_field_array_object_info(
            importer->program, source.record_id, i, &array_info);
        /* A fully materialized flexible member has an incomplete outer
         * ArrayType.  Preserve that ArrayType in sealed Core record metadata,
         * but ask DataLayout for its complete element's storage alignment.
         * A flexible tail contributes zero size to the enclosing record. */
        field_layout_type = field_array && array_info.has_materialized_type &&
                            array_info.is_incomplete && field->is_flexible_array
                                ? array_info.element_type : field->type;
        if (!import_type(importer, field->type, &field_type, depth + 1U) ||
            !minic_data_layout_type(importer->layout, importer->program,
                                    field_layout_type, &field_size, &field_alignment) ||
            !minic_data_layout_record_field_layout(importer->layout, importer->program,
                                                   record, i, &fields[i].byte_offset,
                                                   &fields[i].bit_offset))
            goto failed;
        if (field_array && !array_info.has_materialized_type) {
            size_t array_size = 0U;
            size_t count = array_info.element_count;
            bool layout_known = !array_info.is_incomplete;
            if (count == 0U) goto failed;
            if (layout_known) {
                if (array_info.is_zero_length) array_size = 0U;
                else if (field_size > SIZE_MAX / count) goto failed;
                else array_size = field_size * count;
            }
            if (!import_array(importer, field->type, count, array_info.is_zero_length,
                              !array_info.is_incomplete, array_size, field_alignment,
                              layout_known, &field_type, depth + 1U))
                goto failed;
        }
        fields[i].type_id = field_type;
        fields[i].is_bit_field = field->is_bit_field;
        fields[i].bit_width = field->is_bit_field ? field->bit_width : 0U;
        fields[i].is_packed = field->is_packed;
        fields[i].is_flexible_array = field_array && array_info.is_incomplete;
        fields[i].is_zero_length_array = field_array && array_info.is_zero_length;
        if (field->is_bit_field) {
            if (!bit_field_storage_alignment(record, field, field_alignment,
                                             &fields[i].alignment)) goto failed;
        } else if (!minic_data_layout_record_field_alignment(
                       importer->layout, importer->program, record, i,
                       &fields[i].alignment)) {
            goto failed;
        }
    }
    type.value.record.is_complete = true;
    if (!minic_core_type_complete_record(importer->context, id, &type)) goto failed;
    free(fields);
    *result = id;
    return true;
failed:
    free(fields);
    return false;
}

static bool import_function(MinicCoreTypeImporter *importer, MinicType source,
                            MinicCoreTypeId *result, unsigned depth) {
    const MinicFunctionType *function;
    MinicCoreType type = {0};
    MinicCoreTypeId *params = NULL;
    size_t i;
    if (source.function_type_id >= importer->function_count) return false;
    if (importer->function_types[source.function_type_id] != MINIC_CORE_TYPE_INVALID) {
        *result = importer->function_types[source.function_type_id];
        return true;
    }
    function = minic_c0_program_function_type(importer->program, source.function_type_id);
    if (function == NULL || function->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS)
        return false;
    if (function->parameter_count != 0U) {
        params = malloc(function->parameter_count * sizeof(*params));
        if (params == NULL) return false;
    }
    type.kind = MINIC_CORE_TYPE_FUNCTION;
    type.value.function.parameters = params;
    type.value.function.parameter_count = function->parameter_count;
    type.value.function.is_variadic = function->is_variadic;
    type.value.function.calling_convention = function->calling_convention;
    if (!import_type(importer, function->return_type,
                     &type.value.function.return_type, depth + 1U)) goto failed;
    for (i = 0U; i < function->parameter_count; ++i)
        if (!import_type(importer, function->parameter_types[i], &params[i], depth + 1U))
            goto failed;
    if (!minic_core_type_register(importer->context, &type, result)) goto failed;
    importer->function_types[source.function_type_id] = *result;
    free(params);
    return true;
failed:
    free(params);
    return false;
}

static bool import_vector(MinicCoreTypeImporter *importer, MinicType source,
                          MinicCoreTypeId *result, unsigned depth) {
    MinicCoreType type = {0};
    MinicType element = minic_type_void();
    size_t element_size, element_alignment;
    MinicBuiltinDeclarationRole role;
    type.kind = MINIC_CORE_TYPE_VECTOR;
    role = minic_type_builtin_declaration_role(source.base_kind);
    if (source.base_kind == MINIC_TYPE_BASE_GNU_FIXED_VECTOR) {
        type.value.vector.representation = MINIC_CORE_VECTOR_STORAGE_ONLY;
        element.base_kind = source.gnu_vector_element_kind;
        element.integer_sign = source.integer_sign;
        element.integer_rank = source.integer_rank;
        element.is_plain_char = source.is_plain_char;
    } else if (role == MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE) {
        type.value.vector.representation = MINIC_CORE_VECTOR_REGISTER_VALUE;
        switch (source.base_kind) {
        case MINIC_TYPE_BASE_AARCH64_FLOAT32X4: element = minic_type_float(); break;
        case MINIC_TYPE_BASE_AARCH64_FLOAT64X2: element = minic_type_double(); break;
        case MINIC_TYPE_BASE_AARCH64_UINT64X2: element = minic_type_unsigned_long_long(); break;
        case MINIC_TYPE_BASE_AARCH64_INT8X16: element = minic_type_signed_char(); break;
        case MINIC_TYPE_BASE_AARCH64_UINT8X16:
        case MINIC_TYPE_BASE_AARCH64_POLY8X16: element = minic_type_unsigned_char(); break;
        default: return false;
        }
    } else return false;
    if (!minic_data_layout_type(importer->layout, importer->program,
                                element, &element_size, &element_alignment) ||
        !source_layout(importer, source, &type) || element_size == 0U ||
        type.size % element_size != 0U ||
        !import_type(importer, element, &type.value.vector.element, depth + 1U))
        return false;
    (void)element_alignment;
    type.value.vector.element_count = type.size / element_size;
    return minic_core_type_register(importer->context, &type, result);
}

static bool import_type(MinicCoreTypeImporter *importer, MinicType source,
                        MinicCoreTypeId *result, unsigned depth) {
    MinicCoreType type = {0};
    MinicType pointee;
    const MinicArrayType *array;
    if (depth > 1024U) return false;
    if (minic_type_is_const(source) || minic_type_is_volatile(source) ||
        (source.explicit_alignment != 0U && source.pointer_depth == 0U)) {
        MinicType unqualified;
        const MinicCoreType *base;
        size_t size, alignment;
        if (!minic_type_unqualified(source, &unqualified)) return false;
        unqualified.explicit_alignment = 0U;
        if (!import_type(importer, unqualified, &type.value.qualified.base, depth + 1U))
            return false;
        base = minic_core_type_get(importer->context, type.value.qualified.base);
        if (base == NULL) return false;
        type.kind = MINIC_CORE_TYPE_QUALIFIED;
        type.has_layout = base->has_layout;
        type.size = base->size;
        type.alignment = base->alignment;
        type.is_volatile = minic_type_is_volatile(source);
        type.is_read_only = minic_type_is_const(source);
        type.is_layout_override = source.explicit_alignment != 0U &&
                                  source.pointer_depth == 0U;
        if (type.is_layout_override && type.has_layout) {
            if (!minic_data_layout_type(importer->layout, importer->program,
                                        source, &size, &alignment) || size != type.size)
                return false;
            type.alignment = alignment;
        }
        return minic_core_type_register(importer->context, &type, result);
    }
    if (minic_type_is_pointer(source)) {
        if (!minic_type_pointee(source, &pointee) ||
            !import_type(importer, pointee, &type.value.pointer.pointee, depth + 1U))
            return false;
        type.kind = MINIC_CORE_TYPE_POINTER;
        return source_layout(importer, source, &type) &&
               minic_core_type_register(importer->context, &type, result);
    }
    if (minic_type_is_enum(source)) {
        MinicType integer;
        return minic_c0_type_effective_integer_type(importer->program, source, &integer) &&
               import_type(importer, integer, result, depth + 1U);
    }
    if (minic_type_is_integer(source)) {
        type.kind = MINIC_CORE_TYPE_INTEGER;
        if (!source_layout(importer, source, &type) || type.size > UINT_MAX / 8U)
            return false;
        type.value.integer.bit_width = (unsigned int)(type.size * 8U);
        type.value.integer.is_signed = minic_type_is_signed_integer(source);
        type.value.integer.is_bool = minic_type_is_bool_integer(source);
        return minic_core_type_register(importer->context, &type, result);
    }
    switch (source.base_kind) {
    case MINIC_TYPE_BASE_VOID:
        type.kind = MINIC_CORE_TYPE_VOID;
        return minic_core_type_register(importer->context, &type, result);
    case MINIC_TYPE_BASE_FLOAT: type.value.floating.format = MINIC_CORE_FLOAT_BINARY32; break;
    case MINIC_TYPE_BASE_DOUBLE: type.value.floating.format = MINIC_CORE_FLOAT_BINARY64; break;
    case MINIC_TYPE_BASE_LONG_DOUBLE: type.value.floating.format = MINIC_CORE_FLOAT_EXTENDED; break;
    case MINIC_TYPE_BASE_FLOAT128: type.value.floating.format = MINIC_CORE_FLOAT_BINARY128; break;
    case MINIC_TYPE_BASE_FLOAT32: type.value.floating.format = MINIC_CORE_FLOAT_EXTENDED32; break;
    case MINIC_TYPE_BASE_FLOAT64: type.value.floating.format = MINIC_CORE_FLOAT_EXTENDED64; break;
    case MINIC_TYPE_BASE_FLOAT32X: type.value.floating.format = MINIC_CORE_FLOAT_EXTENDED32X; break;
    case MINIC_TYPE_BASE_FLOAT64X: type.value.floating.format = MINIC_CORE_FLOAT_EXTENDED64X; break;
    case MINIC_TYPE_BASE_ARRAY:
        array = minic_c0_program_array_type(importer->program, source.array_type_id);
        if (array == NULL) return false;
        type.kind = MINIC_CORE_TYPE_ARRAY;
        type.value.array.is_zero_length = array->is_zero_length;
        type.value.array.is_complete = array->element_count != 0U || array->is_zero_length;
        type.value.array.count = array->element_count;
        if (type.value.array.is_complete && !source_layout(importer, source, &type))
            return false;
        return import_array(importer, array->element_type, array->element_count,
                            array->is_zero_length, type.value.array.is_complete,
                            type.size, type.alignment, type.has_layout, result, depth + 1U);
    case MINIC_TYPE_BASE_RECORD:
        return import_record(importer, source, result, depth + 1U);
    case MINIC_TYPE_BASE_FUNCTION:
        return import_function(importer, source, result, depth + 1U);
    case MINIC_TYPE_BASE_GNU_FIXED_VECTOR:
        return import_vector(importer, source, result, depth + 1U);
    default:
        if (minic_type_builtin_declaration_role(source.base_kind) ==
            MINIC_BUILTIN_DECLARATION_FIXED_VECTOR128_STORAGE)
            return import_vector(importer, source, result, depth + 1U);
        if (minic_type_builtin_declaration_role(source.base_kind) ==
            MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE) {
            size_t index;
            switch (source.base_kind) {
            case MINIC_TYPE_BASE_AARCH64_SVE_FLOAT32: index = 0U; break;
            case MINIC_TYPE_BASE_AARCH64_SVE_FLOAT64: index = 1U; break;
            case MINIC_TYPE_BASE_AARCH64_SVE_BOOL: index = 2U; break;
            default: return false;
            }
            if (importer->opaque_types[index] != MINIC_CORE_TYPE_INVALID) {
                *result = importer->opaque_types[index];
                return true;
            }
            type.kind = MINIC_CORE_TYPE_OPAQUE;
            /* A declaration is representable, but cannot be stored or passed. */
            if (!minic_core_type_register(importer->context, &type, result)) return false;
            importer->opaque_types[index] = *result;
            return true;
        }
        return false;
    }
    type.kind = MINIC_CORE_TYPE_FLOAT;
    return source_layout(importer, source, &type) &&
           minic_core_type_register(importer->context, &type, result);
}

bool minic_core_type_import(MinicCoreTypeImporter *importer,
                            MinicType source, MinicCoreTypeId *result) {
    MinicCoreTypeId imported = MINIC_CORE_TYPE_INVALID;
    if (result != NULL) *result = MINIC_CORE_TYPE_INVALID;
    if (importer == NULL || importer->failed || importer->context == NULL ||
        importer->layout == NULL || importer->program == NULL || result == NULL)
        return false;
    if (!import_type(importer, source, &imported, 0U)) {
        importer->failed = true;
        return false;
    }
    *result = imported;
    return true;
}
