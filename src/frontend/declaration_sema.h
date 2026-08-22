#ifndef MINIC_FRONTEND_DECLARATION_SEMA_H
#define MINIC_FRONTEND_DECLARATION_SEMA_H

#include "frontend/ast.h"
#include "frontend/semantic_snapshot.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

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
minic_declaration_materialize_array_suffix(
    MinicC0Program *program,
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
            program,
            return_type,
            parameter_types,
            parameter_count,
            is_variadic,
            &result)) {
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
