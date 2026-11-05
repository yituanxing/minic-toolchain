#include "core/core_type.h"

#include <stdlib.h>
#include <string.h>

static void type_destroy(MinicCoreType *type) {
    if (type == NULL) return;
    if (type->kind == MINIC_CORE_TYPE_RECORD) free(type->value.record.fields);
    if (type->kind == MINIC_CORE_TYPE_FUNCTION) free(type->value.function.parameters);
    (void)memset(type, 0, sizeof(*type));
}

void minic_core_type_context_initialize(MinicCoreTypeContext *context) {
    if (context != NULL) (void)memset(context, 0, sizeof(*context));
}

void minic_core_type_context_destroy(MinicCoreTypeContext *context) {
    size_t i;
    if (context == NULL) return;
    for (i = 0U; i < context->type_count; ++i) type_destroy(&context->types[i]);
    free(context->types);
    minic_core_type_context_initialize(context);
}

const MinicCoreType *minic_core_type_get(const MinicCoreTypeContext *context,
                                        MinicCoreTypeId id) {
    if (context == NULL || id >= context->type_count) return NULL;
    return &context->types[id];
}

MinicCoreTypeId minic_core_type_unqualified(const MinicCoreTypeContext *context,
                                            MinicCoreTypeId id) {
    const MinicCoreType *type;
    size_t remaining;
    if (context == NULL) return MINIC_CORE_TYPE_INVALID;
    remaining = context->type_count;
    while ((type = minic_core_type_get(context, id)) != NULL &&
           type->kind == MINIC_CORE_TYPE_ALIGNED) {
        if (remaining-- == 0U) return MINIC_CORE_TYPE_INVALID;
        id = type->value.aligned.base;
    }
    return type != NULL ? id : MINIC_CORE_TYPE_INVALID;
}

static bool type_equal(const MinicCoreType *left, const MinicCoreType *right) {
    if (left->kind != right->kind || left->has_layout != right->has_layout ||
        left->size != right->size || left->alignment != right->alignment ||
        left->has_abi_alignment_override != right->has_abi_alignment_override)
        return false;
    switch (left->kind) {
    case MINIC_CORE_TYPE_VOID: return true;
    case MINIC_CORE_TYPE_INTEGER:
        return left->value.integer.bit_width == right->value.integer.bit_width &&
               left->value.integer.is_signed == right->value.integer.is_signed &&
               left->value.integer.is_bool == right->value.integer.is_bool;
    case MINIC_CORE_TYPE_FLOAT:
        return left->value.floating.format == right->value.floating.format;
    case MINIC_CORE_TYPE_POINTER:
        return left->value.pointer.pointee == right->value.pointer.pointee;
    case MINIC_CORE_TYPE_ARRAY:
        return left->value.array.element == right->value.array.element &&
               left->value.array.count == right->value.array.count &&
               left->value.array.is_complete == right->value.array.is_complete;
    /* Existing indirect-call admission distinguishes declared function type
     * identities even when their storage signatures happen to be equal. */
    case MINIC_CORE_TYPE_FUNCTION: return false;
    case MINIC_CORE_TYPE_VECTOR:
        return left->value.vector.element == right->value.vector.element &&
               left->value.vector.element_count == right->value.vector.element_count &&
               left->value.vector.representation == right->value.vector.representation;
    case MINIC_CORE_TYPE_ALIGNED:
        return left->value.aligned.base == right->value.aligned.base;
    /* Record and opaque identities are nominal, not shape-interned. */
    case MINIC_CORE_TYPE_RECORD:
    case MINIC_CORE_TYPE_OPAQUE:
    default: return false;
    }
}

static bool type_references_valid(const MinicCoreTypeContext *context,
                                  const MinicCoreType *type) {
    size_t i;
    if (type->has_layout && type->alignment == 0U) return false;
    switch (type->kind) {
    case MINIC_CORE_TYPE_VOID:
    case MINIC_CORE_TYPE_INTEGER:
        return true;
    case MINIC_CORE_TYPE_FLOAT:
        return type->value.floating.format == MINIC_CORE_FLOAT_BINARY32
                   ? (!type->has_layout || type->size == 4U)
               : type->value.floating.format == MINIC_CORE_FLOAT_BINARY64
                   ? (!type->has_layout || type->size == 8U)
               : minic_core_float_format_is_wide(type->value.floating.format)
                   ? (!type->has_layout || type->size == 16U)
                   : false;
    case MINIC_CORE_TYPE_POINTER:
        return type->value.pointer.pointee < context->type_count;
    case MINIC_CORE_TYPE_ARRAY: {
        const MinicCoreType *element = minic_core_type_get(
            context, type->value.array.element);
        size_t count = type->value.array.count;

        if (element == NULL) return false;
        /* Incomplete arrays are legitimate type identities (for example a
         * pointer to an unknown-bound array), but cannot describe storage.
         * An incomplete extent has no storage; a complete array with count
         * zero is the independently represented zero-extent case. */
        if (!type->value.array.is_complete)
            return count == 0U && !type->has_layout && type->size == 0U &&
                   type->alignment == 0U;

        /* A complete array owns its entire object extent. Core objects must
         * not repeat the source declarator dimension separately. Its natural
         * alignment comes from the element, while an explicitly overridden
         * execution view may strengthen that alignment. */
        if (!type->has_layout || !element->has_layout ||
            element->alignment == 0U || type->alignment < element->alignment ||
            (type->alignment != element->alignment &&
             (!type->has_abi_alignment_override ||
              (type->alignment & (type->alignment - 1U)) != 0U)))
            return false;
        if (count == 0U) return type->size == 0U;
        if (element->size > SIZE_MAX / count)
            return false;
        return type->size == element->size * count;
    }
    case MINIC_CORE_TYPE_RECORD:
        if (type->value.record.field_count > SIZE_MAX / sizeof(*type->value.record.fields) ||
            (type->value.record.field_count != 0U && type->value.record.fields == NULL))
            return false;
        for (i = 0U; i < type->value.record.field_count; ++i)
            if (type->value.record.fields[i].type_id >= context->type_count)
                return false;
        return true;
    case MINIC_CORE_TYPE_FUNCTION:
        if (type->value.function.parameter_count >
                SIZE_MAX / sizeof(*type->value.function.parameters) ||
            type->value.function.return_type >= context->type_count ||
            !minic_function_calling_convention_valid(type->value.function.calling_convention) ||
            (type->value.function.parameter_count != 0U &&
             type->value.function.parameters == NULL))
            return false;
        for (i = 0U; i < type->value.function.parameter_count; ++i)
            if (type->value.function.parameters[i] >= context->type_count)
                return false;
        return true;
    case MINIC_CORE_TYPE_VECTOR:
        return type->value.vector.element < context->type_count &&
               type->value.vector.element_count != 0U &&
               (type->value.vector.representation == MINIC_CORE_VECTOR_STORAGE_ONLY ||
                type->value.vector.representation == MINIC_CORE_VECTOR_REGISTER_VALUE);
    case MINIC_CORE_TYPE_OPAQUE:
        return !type->has_layout;
    case MINIC_CORE_TYPE_ALIGNED: {
        const MinicCoreType *base = minic_core_type_get(context, type->value.aligned.base);
        return base != NULL &&
               type->has_abi_alignment_override &&
               base->has_layout == type->has_layout &&
               base->size == type->size &&
               (base->alignment == type->alignment ||
                (type->has_abi_alignment_override && type->alignment != 0U &&
                 (type->alignment & (type->alignment - 1U)) == 0U));
    }
    default: return false;
    }
}

static bool copy_owned(MinicCoreType *dest, const MinicCoreType *source) {
    *dest = *source;
    if (source->kind == MINIC_CORE_TYPE_RECORD) {
        dest->value.record.fields = NULL;
        if (source->value.record.field_count != 0U) {
            if (source->value.record.fields == NULL ||
                source->value.record.field_count > SIZE_MAX / sizeof(*source->value.record.fields))
                return false;
            dest->value.record.fields = malloc(source->value.record.field_count *
                                               sizeof(*source->value.record.fields));
            if (dest->value.record.fields == NULL) return false;
            (void)memcpy(dest->value.record.fields, source->value.record.fields,
                         source->value.record.field_count * sizeof(*source->value.record.fields));
        }
    } else if (source->kind == MINIC_CORE_TYPE_FUNCTION) {
        dest->value.function.parameters = NULL;
        if (source->value.function.parameter_count != 0U) {
            if (source->value.function.parameters == NULL ||
                source->value.function.parameter_count > SIZE_MAX / sizeof(*source->value.function.parameters))
                return false;
            dest->value.function.parameters = malloc(source->value.function.parameter_count *
                                                    sizeof(*source->value.function.parameters));
            if (dest->value.function.parameters == NULL) return false;
            (void)memcpy(dest->value.function.parameters, source->value.function.parameters,
                         source->value.function.parameter_count *
                         sizeof(*source->value.function.parameters));
        }
    }
    return true;
}

bool minic_core_type_register(MinicCoreTypeContext *context,
                              const MinicCoreType *type,
                              MinicCoreTypeId *id) {
    MinicCoreType copied;
    size_t capacity;
    MinicCoreType *resized;
    size_t i;
    if (context == NULL || type == NULL || id == NULL ||
        type->kind > MINIC_CORE_TYPE_ALIGNED || !type_references_valid(context, type))
        return false;
    if (type->kind != MINIC_CORE_TYPE_RECORD && type->kind != MINIC_CORE_TYPE_OPAQUE &&
        type->kind != MINIC_CORE_TYPE_FUNCTION)
        for (i = 0U; i < context->type_count; ++i)
            if (type_equal(&context->types[i], type)) {
                *id = i;
                return true;
            }
    if (context->type_count == SIZE_MAX) return false;
    if (context->type_count == context->type_capacity) {
        capacity = context->type_capacity != 0U ? context->type_capacity * 2U : 16U;
        if (capacity < context->type_count + 1U) capacity = context->type_count + 1U;
        if (capacity > SIZE_MAX / sizeof(*context->types)) return false;
        resized = realloc(context->types, capacity * sizeof(*context->types));
        if (resized == NULL) return false;
        context->types = resized;
        context->type_capacity = capacity;
    }
    if (!copy_owned(&copied, type)) return false;
    *id = context->type_count;
    context->types[context->type_count++] = copied;
    return true;
}

bool minic_core_type_complete_record(MinicCoreTypeContext *context,
                                     MinicCoreTypeId id,
                                     const MinicCoreType *complete) {
    MinicCoreType copied;
    MinicCoreType *current;
    if (context == NULL || complete == NULL ||
        complete->kind != MINIC_CORE_TYPE_RECORD ||
        !complete->value.record.is_complete || !complete->has_layout ||
        complete->alignment == 0U || id >= context->type_count ||
        !type_references_valid(context, complete))
        return false;
    current = &context->types[id];
    if (current->kind != MINIC_CORE_TYPE_RECORD || current->value.record.is_complete)
        return false;
    if (current->value.record.is_union != complete->value.record.is_union ||
        current->value.record.has_nondefault_layout_policy !=
            complete->value.record.has_nondefault_layout_policy ||
        (current->has_layout &&
         (current->size != complete->size ||
          current->alignment != complete->alignment)))
        return false;
    if (!copy_owned(&copied, complete)) return false;
    type_destroy(current);
    *current = copied;
    return true;
}
