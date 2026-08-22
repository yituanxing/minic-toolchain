#ifndef MINIC_FRONTEND_DECLARATION_SEMA_H
#define MINIC_FRONTEND_DECLARATION_SEMA_H

#include "frontend/ast.h"
#include "frontend/semantic_snapshot.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MINIC_DECLARATION_MAX_ARRAY_DIMENSIONS 8U

typedef struct MinicDeclarationArraySuffix {
    size_t bounds[MINIC_DECLARATION_MAX_ARRAY_DIMENSIONS];
    size_t dimension_count;
    unsigned int zero_length_mask;
    bool outermost_incomplete;
} MinicDeclarationArraySuffix;

typedef enum MinicDeclarationArrayMaterializeStatus {
    MINIC_DECLARATION_ARRAY_MATERIALIZE_OK = 0,
    MINIC_DECLARATION_ARRAY_MATERIALIZE_INVALID,
    MINIC_DECLARATION_ARRAY_MATERIALIZE_INCOMPLETE_FAILED,
    MINIC_DECLARATION_ARRAY_MATERIALIZE_ZERO_LENGTH_FAILED,
    MINIC_DECLARATION_ARRAY_MATERIALIZE_FIXED_FAILED,
    MINIC_DECLARATION_ARRAY_MATERIALIZE_TRANSACTION_ESCAPED
} MinicDeclarationArrayMaterializeStatus;

typedef struct MinicDeclarationExternalObjectAttributes {
    const char *section_name;
    size_t section_name_length;
    size_t explicit_alignment;
    MinicSymbolVisibility visibility;
    bool has_section;
    bool has_visibility;
} MinicDeclarationExternalObjectAttributes;

typedef enum MinicDeclarationExternalObjectMergeStatus {
    MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_OK = 0,
    MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_INVALID,
    MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_TYPE_CONFLICT,
    MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_ATTRIBUTE_CONFLICT,
    MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_COMMIT_FAILED
} MinicDeclarationExternalObjectMergeStatus;

static inline bool minic_declaration_array_suffix_valid(const MinicDeclarationArraySuffix *suffix) {
    unsigned int valid_zero_length_bits;

    if (suffix == NULL || suffix->dimension_count > MINIC_DECLARATION_MAX_ARRAY_DIMENSIONS) {
        return false;
    }
    if (suffix->dimension_count == 0U) {
        return !suffix->outermost_incomplete && suffix->zero_length_mask == 0U;
    }
    valid_zero_length_bits = (1U << suffix->dimension_count) - 1U;
    if ((suffix->zero_length_mask & ~valid_zero_length_bits) != 0U) {
        return false;
    }
    return !suffix->outermost_incomplete || (suffix->zero_length_mask & 1U) == 0U;
}

static inline MinicDeclarationArrayMaterializeStatus
minic_declaration_materialize_array_suffix(MinicC0Program *program,
                                           MinicType element_type,
                                           const MinicDeclarationArraySuffix *suffix,
                                           MinicType *type) {
    MinicSemanticSnapshot snapshot;
    size_t dimension;
    MinicType result;

    if (program == NULL || type == NULL || !minic_declaration_array_suffix_valid(suffix)) {
        return MINIC_DECLARATION_ARRAY_MATERIALIZE_INVALID;
    }
    snapshot = minic_semantic_snapshot_capture(program);
    result = element_type;
    dimension = suffix->dimension_count;
    while (dimension > 0U) {
        unsigned int bit;
        MinicDeclarationArrayMaterializeStatus failure;
        bool added;

        dimension -= 1U;
        bit = 1U << dimension;
        if (dimension == 0U && suffix->outermost_incomplete) {
            added = minic_c0_program_add_incomplete_array_type(program, result, &result);
            failure = MINIC_DECLARATION_ARRAY_MATERIALIZE_INCOMPLETE_FAILED;
        } else if ((suffix->zero_length_mask & bit) != 0U) {
            added = minic_c0_program_add_zero_length_array_type(program, result, &result);
            failure = MINIC_DECLARATION_ARRAY_MATERIALIZE_ZERO_LENGTH_FAILED;
        } else {
            added = minic_c0_program_add_array_type(
                program, result, suffix->bounds[dimension], &result);
            failure = MINIC_DECLARATION_ARRAY_MATERIALIZE_FIXED_FAILED;
        }
        if (!added) {
            if (!minic_semantic_snapshot_rollback_declarator_types(&snapshot, program)) {
                return MINIC_DECLARATION_ARRAY_MATERIALIZE_TRANSACTION_ESCAPED;
            }
            return failure;
        }
    }
    *type = result;
    return MINIC_DECLARATION_ARRAY_MATERIALIZE_OK;
}

/* Declaration Sema owns external object type compatibility and composite-array
 * mutation. Parser-facing helpers may forward here, but must not duplicate these rules. */
static inline bool minic_declaration_external_object_types_compatible(const MinicC0Program *program,
                                                                      MinicType existing_type,
                                                                      MinicType declared_type) {
    const MinicArrayType *existing_array;
    const MinicArrayType *declared_array;

    if (minic_type_equal(existing_type, declared_type)) {
        return true;
    }
    if (program == NULL || !minic_type_is_array(existing_type) ||
        !minic_type_is_array(declared_type)) {
        return false;
    }
    existing_array = minic_c0_program_array_type(program, existing_type.array_type_id);
    declared_array = minic_c0_program_array_type(program, declared_type.array_type_id);
    if (existing_array == NULL || declared_array == NULL ||
        !minic_declaration_external_object_types_compatible(
            program, existing_array->element_type, declared_array->element_type)) {
        return false;
    }
    if ((existing_array->element_count == 0U && !existing_array->is_zero_length) ||
        (declared_array->element_count == 0U && !declared_array->is_zero_length)) {
        return true;
    }
    return existing_array->is_zero_length == declared_array->is_zero_length &&
           existing_array->element_count == declared_array->element_count;
}

static inline bool minic_declaration_merge_external_array_composite_type(MinicC0Program *program,
                                                                         MinicType existing_type,
                                                                         MinicType declared_type) {
    const MinicArrayType *existing_array;
    const MinicArrayType *declared_array;
    MinicType existing_element;
    MinicType declared_element;
    size_t declared_count;

    if (minic_type_equal(existing_type, declared_type)) {
        return true;
    }
    if (!minic_declaration_external_object_types_compatible(
            program, existing_type, declared_type)) {
        return false;
    }
    if (program == NULL || !minic_type_is_array(existing_type) ||
        !minic_type_is_array(declared_type)) {
        return false;
    }
    existing_array = minic_c0_program_array_type(program, existing_type.array_type_id);
    declared_array = minic_c0_program_array_type(program, declared_type.array_type_id);
    if (existing_array == NULL || declared_array == NULL) {
        return false;
    }
    existing_element = existing_array->element_type;
    declared_element = declared_array->element_type;
    declared_count = declared_array->element_count;
    if (!minic_declaration_merge_external_array_composite_type(
            program, existing_element, declared_element)) {
        return false;
    }
    existing_array = minic_c0_program_array_type(program, existing_type.array_type_id);
    if (existing_array == NULL) {
        return false;
    }
    if (existing_array->element_count == 0U && !existing_array->is_zero_length) {
        if (declared_array->is_zero_length) {
            return minic_c0_program_complete_zero_length_array_type(program, existing_type);
        }
        if (declared_count != 0U) {
            return minic_c0_program_complete_array_type(program, existing_type, declared_count);
        }
        return true;
    }
    if (declared_count == 0U && !declared_array->is_zero_length) {
        return true;
    }
    return existing_array->is_zero_length == declared_array->is_zero_length &&
           existing_array->element_count == declared_count;
}

static inline bool minic_declaration_external_object_attributes_valid(
    const MinicDeclarationExternalObjectAttributes *attributes) {
    size_t alignment;

    if (attributes == NULL) {
        return false;
    }
    if (attributes->has_section &&
        (attributes->section_name == NULL || attributes->section_name_length == 0U ||
         attributes->section_name_length == SIZE_MAX)) {
        return false;
    }
    alignment = attributes->explicit_alignment;
    if (alignment != 0U && (alignment & (alignment - 1U)) != 0U) {
        return false;
    }
    return !attributes->has_visibility ||
           (attributes->visibility >= MINIC_SYMBOL_VISIBILITY_DEFAULT &&
            attributes->visibility <= MINIC_SYMBOL_VISIBILITY_PROTECTED);
}

static inline MinicDeclarationExternalObjectMergeStatus minic_declaration_merge_external_object(
    MinicC0Program *program,
    MinicGlobalObjectId object_id,
    MinicType declared_type,
    const MinicDeclarationExternalObjectAttributes *attributes) {
    const MinicGlobalObject *object;

    if (program == NULL || object_id >= program->global_object_count ||
        !minic_declaration_external_object_attributes_valid(attributes)) {
        return MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_INVALID;
    }
    object = minic_c0_program_global_object(program, object_id);
    if (object == NULL || !minic_declaration_external_object_types_compatible(
                              program, object->type, declared_type)) {
        return MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_TYPE_CONFLICT;
    }
    if (attributes->has_section && object->section_name != NULL &&
        (object->section_name_length != attributes->section_name_length ||
         memcmp(object->section_name, attributes->section_name, attributes->section_name_length) !=
             0)) {
        return MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_ATTRIBUTE_CONFLICT;
    }
    if (attributes->has_visibility && object->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
        object->visibility != attributes->visibility) {
        return MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_ATTRIBUTE_CONFLICT;
    }

    /* All semantic conflicts are rejected above. A new section is the only
     * commit step that may allocate, so perform it before composite-type mutation.
     * Array completion and the remaining metadata setters are allocation-free after
     * this preflight and therefore cannot introduce a semantic half-commit. */
    if ((attributes->has_section &&
         !minic_c0_global_object_set_section(program,
                                             object_id,
                                             attributes->section_name,
                                             attributes->section_name_length)) ||
        (minic_type_is_array(object->type) &&
         !minic_declaration_merge_external_array_composite_type(
             program, object->type, declared_type)) ||
        (attributes->explicit_alignment != 0U &&
         !minic_c0_global_object_set_explicit_alignment(
             program, object_id, attributes->explicit_alignment)) ||
        (attributes->has_visibility &&
         !minic_c0_global_object_set_visibility(program, object_id, attributes->visibility))) {
        return MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_COMMIT_FAILED;
    }
    return MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_OK;
}

static inline bool
minic_declaration_build_function_type(MinicC0Program *program,
                                      MinicType return_type,
                                      const MinicType *parameter_types,
                                      size_t parameter_count,
                                      bool is_variadic,
                                      size_t pointer_depth,
                                      unsigned int pointer_const_qualifiers,
                                      unsigned int pointer_volatile_qualifiers,
                                      const MinicDeclarationArraySuffix *array_suffix,
                                      MinicType *type) {
    MinicSemanticSnapshot snapshot;
    MinicDeclarationArrayMaterializeStatus array_status;
    unsigned int valid_pointer_bits;
    MinicType result;
    size_t level;

    if (program == NULL || type == NULL || parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (parameter_count != 0U && parameter_types == NULL) ||
        pointer_depth > sizeof(unsigned int) * CHAR_BIT ||
        !minic_declaration_array_suffix_valid(array_suffix)) {
        return false;
    }
    if (pointer_depth == 0U) {
        valid_pointer_bits = 0U;
    } else if (pointer_depth == sizeof(unsigned int) * CHAR_BIT) {
        valid_pointer_bits = UINT_MAX;
    } else {
        valid_pointer_bits = (1U << pointer_depth) - 1U;
    }
    if (((pointer_const_qualifiers | pointer_volatile_qualifiers) & ~valid_pointer_bits) != 0U) {
        return false;
    }

    snapshot = minic_semantic_snapshot_capture(program);
    if (!minic_c0_program_add_variadic_function_type(
            program, return_type, parameter_types, parameter_count, is_variadic, &result)) {
        (void)minic_semantic_snapshot_rollback_declarator_types(&snapshot, program);
        return false;
    }
    for (level = 0U; level < pointer_depth; ++level) {
        unsigned int bit;

        if (!minic_type_pointer_to(result, &result)) {
            (void)minic_semantic_snapshot_rollback_declarator_types(&snapshot, program);
            return false;
        }
        bit = 1U << level;
        if ((pointer_const_qualifiers & bit) != 0U && !minic_type_add_const(result, &result)) {
            (void)minic_semantic_snapshot_rollback_declarator_types(&snapshot, program);
            return false;
        }
        if ((pointer_volatile_qualifiers & bit) != 0U &&
            !minic_type_add_volatile(result, &result)) {
            (void)minic_semantic_snapshot_rollback_declarator_types(&snapshot, program);
            return false;
        }
    }

    array_status =
        minic_declaration_materialize_array_suffix(program, result, array_suffix, &result);
    if (array_status != MINIC_DECLARATION_ARRAY_MATERIALIZE_OK) {
        (void)minic_semantic_snapshot_rollback_declarator_types(&snapshot, program);
        return false;
    }
    *type = result;
    return true;
}

#endif
