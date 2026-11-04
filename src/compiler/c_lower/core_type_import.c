#include "compiler/c_lower/core_type_import.h"
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
                                        const MinicTargetInvocation *invocation,
                                        MinicCoreTypeContext *context) {
    size_t i;
    if (importer == NULL) return false;
    (void)memset(importer, 0, sizeof(*importer));
    if (program == NULL || invocation == NULL ||
        invocation->machine.data_layout == NULL || context == NULL) {
        importer->failed = true;
        return false;
    }
    importer->program = program;
    importer->invocation = invocation;
    importer->context = context;
    importer->record_count = program->record_count;
    importer->function_count = program->function_type_count;
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
    free(importer->opaque_types);
    (void)memset(importer, 0, sizeof(*importer));
}

static bool source_layout(MinicCoreTypeImporter *importer, MinicType source,
                          MinicCoreType *type) {
    if (!minic_data_layout_type(importer->invocation, importer->program,
                                source, &type->size, &type->alignment))
        return false;
    type->has_layout = true;
    return true;
}

static bool import_array(MinicCoreTypeImporter *importer, MinicType element,
                         size_t count, bool complete,
                         size_t size, size_t alignment, bool has_layout,
                         MinicCoreTypeId *result, unsigned depth) {
    MinicCoreType type = {0};
    if (!import_type(importer, element, &type.value.array.element, depth + 1U))
        return false;
    type.kind = MINIC_CORE_TYPE_ARRAY;
    type.value.array.count = count;
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
    type.value.record.has_nondefault_layout_policy =
        record->is_packed || record->pack_alignment != 0U ||
        record->explicit_alignment != 0U;
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
        const MinicCoreType *array_storage;
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
        field_layout_type = field_array && array_info.is_incomplete &&
                            field->is_flexible_array
                                ? array_info.element_type : field->type;
        if (!import_type(importer, field->type, &field_type, depth + 1U) ||
            !minic_data_layout_type(importer->invocation, importer->program,
                                    field_layout_type, &field_size, &field_alignment) ||
            !minic_data_layout_record_field_layout(importer->invocation, importer->program,
                                                   record, i, &fields[i].byte_offset,
                                                   &fields[i].bit_offset))
            goto failed;
        /* Record field types are already complete source ArrayTypes (or a
         * legal incomplete flexible tail).  Type import must not synthesize
         * a second array dimension from declarator metadata. */
        if (field_array) {
            array_storage = minic_core_type_get(
                importer->context,
                minic_core_type_unqualified(importer->context, field_type));
            if (!array_info.has_materialized_type || array_storage == NULL ||
                array_storage->kind != MINIC_CORE_TYPE_ARRAY ||
                array_storage->value.array.is_complete == array_info.is_incomplete ||
                (array_storage->value.array.is_complete &&
                 array_storage->value.array.count == 0U) != array_info.is_zero_length)
                goto failed;
        }
        fields[i].type_id = field_type;
        fields[i].is_bit_field = field->is_bit_field;
        fields[i].bit_width = field->is_bit_field ? field->bit_width : 0U;
        fields[i].has_nondefault_layout_policy = field->is_packed;
        if (!minic_data_layout_record_field_storage_alignment(
                importer->invocation, importer->program, record, i,
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

    /* C declarations can manufacture distinct source FunctionType IDs for
     * the same fully resolved execution signature (notably a function
     * pointer record field versus its initializer).  This importer-local
     * canonicalization keeps their nested POINTER CoreTypes identical; the
     * Core arena itself still preserves independent producers' nominal
     * function identities and the indirect-call verifier's exact TypeId
     * contract.  Compare EVERY ABI-relevant signature component rather than
     * merely return/parameter register classes. */
    for (i = 0U; i < importer->function_count; ++i) {
        const MinicCoreType *existing;
        MinicCoreTypeId previous = importer->function_types[i];
        if (previous == MINIC_CORE_TYPE_INVALID) continue;
        existing = minic_core_type_get(importer->context, previous);
        if (existing == NULL || existing->kind != MINIC_CORE_TYPE_FUNCTION ||
            existing->value.function.return_type != type.value.function.return_type ||
            existing->value.function.parameter_count != function->parameter_count ||
            existing->value.function.is_variadic != function->is_variadic ||
            existing->value.function.calling_convention != function->calling_convention ||
            (function->parameter_count != 0U &&
             (existing->value.function.parameters == NULL ||
              memcmp(existing->value.function.parameters, params,
                     function->parameter_count * sizeof(*params)) != 0)))
            continue;
        *result = previous;
        importer->function_types[source.function_type_id] = previous;
        free(params);
        return true;
    }
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
    MinicTargetSourceBuiltinTypeDescriptor descriptor;
    type.kind = MINIC_CORE_TYPE_VECTOR;
    if (source.base_kind == MINIC_TYPE_BASE_GNU_FIXED_VECTOR) {
        type.value.vector.representation = MINIC_CORE_VECTOR_STORAGE_ONLY;
        element.base_kind = source.gnu_vector_element_kind;
        element.integer_sign = source.integer_sign;
        element.integer_rank = source.integer_rank;
        element.is_plain_char = source.is_plain_char;
    } else if (minic_target_info_source_builtin_type_descriptor(
                   importer->invocation, source, &descriptor) &&
               descriptor.core_import == MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128) {
        type.value.vector.representation = MINIC_CORE_VECTOR_REGISTER_VALUE;
        switch (descriptor.vector_element) {
        case MINIC_TARGET_BUILTIN_SCALAR_FLOAT32: element = minic_type_float(); break;
        case MINIC_TARGET_BUILTIN_SCALAR_FLOAT64: element = minic_type_double(); break;
        case MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_LONG_LONG:
            element = minic_type_unsigned_long_long(); break;
        case MINIC_TARGET_BUILTIN_SCALAR_SIGNED_CHAR: element = minic_type_signed_char(); break;
        case MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_CHAR: element = minic_type_unsigned_char(); break;
        default: return false;
        }
    } else return false;
    if (!minic_data_layout_type(importer->invocation, importer->program,
                                element, &element_size, &element_alignment) ||
        !source_layout(importer, source, &type) || element_size == 0U ||
        type.size % element_size != 0U ||
        !import_type(importer, element, &type.value.vector.element, depth + 1U))
        return false;
    (void)element_alignment;
    type.value.vector.element_count = type.size / element_size;
    return minic_core_type_register(importer->context, &type, result);
}

/* A source type with no complete object representation can still appear
 * beneath a pointer or in an uncalled function-pointer signature.  Preserve
 * nominal identity for both incomplete C enums and target header opaque types
 * without pretending the former have an integer ABI or a storage layout. */
static bool import_source_opaque(MinicCoreTypeImporter *importer,
                                 size_t source_id, bool is_incomplete_enum,
                                 MinicCoreTypeId *result) {
    MinicCoreOpaqueSourceTypeCacheEntry *entries;
    MinicCoreType type = {0};
    MinicCoreTypeId id;
    size_t i, next_capacity;
    for (i = 0U; i < importer->opaque_type_count; ++i) {
        if (importer->opaque_types[i].source_id == source_id &&
            importer->opaque_types[i].is_incomplete_enum == is_incomplete_enum) {
            *result = importer->opaque_types[i].type_id;
            return true;
        }
    }
    if (importer->opaque_type_count == importer->opaque_type_capacity) {
        next_capacity = importer->opaque_type_capacity == 0U
            ? 8U : importer->opaque_type_capacity * 2U;
        if (next_capacity <= importer->opaque_type_capacity ||
            next_capacity > SIZE_MAX / sizeof(*entries)) return false;
        entries = realloc(importer->opaque_types, next_capacity * sizeof(*entries));
        if (entries == NULL) return false;
        importer->opaque_types = entries;
        importer->opaque_type_capacity = next_capacity;
    }
    type.kind = MINIC_CORE_TYPE_OPAQUE;
    if (!minic_core_type_register(importer->context, &type, &id)) return false;
    entries = importer->opaque_types;
    entries[importer->opaque_type_count].source_id = source_id;
    entries[importer->opaque_type_count].is_incomplete_enum = is_incomplete_enum;
    entries[importer->opaque_type_count].type_id = id;
    importer->opaque_type_count++;
    *result = id;
    return true;
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
        type.has_abi_alignment_override = source.explicit_alignment != 0U &&
                                          source.pointer_depth == 0U;
        if (type.has_abi_alignment_override && type.has_layout) {
            if (!minic_data_layout_type(importer->invocation, importer->program,
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
        const MinicEnum *source_enum =
            minic_c0_program_enum(importer->program, source.enum_id);
        MinicType integer;
        if (source_enum == NULL) return false;
        if (!source_enum->is_complete)
            return import_source_opaque(importer, source.enum_id, true, result);
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
        type.value.array.is_complete = array->element_count != 0U || array->is_zero_length;
        type.value.array.count = array->element_count;
        if (type.value.array.is_complete && !source_layout(importer, source, &type))
            return false;
        return import_array(importer, array->element_type, array->element_count,
                            type.value.array.is_complete,
                            type.size, type.alignment, type.has_layout, result, depth + 1U);
    case MINIC_TYPE_BASE_RECORD:
        return import_record(importer, source, result, depth + 1U);
    case MINIC_TYPE_BASE_FUNCTION:
        return import_function(importer, source, result, depth + 1U);
    case MINIC_TYPE_BASE_GNU_FIXED_VECTOR:
        return import_vector(importer, source, result, depth + 1U);
    case MINIC_TYPE_BASE_TARGET_BUILTIN: {
        MinicTargetSourceBuiltinTypeDescriptor descriptor;
        if (!minic_target_info_source_builtin_type_descriptor(
                importer->invocation, source, &descriptor)) return false;
        switch (descriptor.core_import) {
        case MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128:
            return import_vector(importer, source, result, depth + 1U);
        case MINIC_TARGET_BUILTIN_CORE_IMPORT_OPAQUE:
            return import_source_opaque(importer, source.target_builtin_id, false, result);
        default:
            return false;
        }
    }
    default: return false;
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
        importer->invocation == NULL || importer->program == NULL || result == NULL)
        return false;
    if (!import_type(importer, source, &imported, 0U)) {
        importer->failed = true;
        return false;
    }
    *result = imported;
    return true;
}
