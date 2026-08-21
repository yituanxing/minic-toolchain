#ifndef MINIC_FRONTEND_SEMA_H
#define MINIC_FRONTEND_SEMA_H

#include "frontend/ast.h"
#include "frontend/declarator.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Array declarator syntax is intentionally transient. Parsing fills this value
 * without growing Program-owned type arenas; semantic code materializes the
 * canonical semantic type only at an explicit commit point.
 *
 * This is the first Declaration/Sema transaction seam. Keep target, ABI, OS,
 * and object-format facts out of this representation.
 */
static inline bool
minic_sema_array_declarator_compatible_with_type(const MinicC0Program *program,
                                                 MinicType existing_type,
                                                 MinicType element_type,
                                                 const MinicArrayDeclaratorSyntax *declarator) {
    size_t dimension;

    if (program == NULL || declarator == NULL || declarator->dimension_count == 0U ||
        declarator->dimension_count > MINIC_ARRAY_DECLARATOR_MAX_DIMENSIONS) {
        return false;
    }

    for (dimension = 0U; dimension < declarator->dimension_count; ++dimension) {
        const MinicArrayType *existing_array;
        unsigned int bit;
        bool existing_incomplete;
        bool declared_incomplete;
        bool declared_zero_length;

        if (!minic_type_is_array(existing_type)) {
            return false;
        }
        existing_array = minic_c0_program_array_type(program, existing_type.array_type_id);
        if (existing_array == NULL) {
            return false;
        }
        bit = 1U << dimension;
        existing_incomplete =
            existing_array->element_count == 0U && !existing_array->is_zero_length;
        declared_incomplete = dimension == 0U && declarator->outermost_incomplete;
        declared_zero_length = (declarator->zero_length_mask & bit) != 0U;

        if (!declared_incomplete && !existing_incomplete) {
            if (declared_zero_length != existing_array->is_zero_length) {
                return false;
            }
            if (!declared_zero_length &&
                declarator->bounds[dimension] != existing_array->element_count) {
                return false;
            }
        }
        existing_type = existing_array->element_type;
    }
    return minic_type_equal(existing_type, element_type);
}

static inline bool
minic_sema_merge_array_declarator_composite_type(MinicC0Program *program,
                                                  MinicType existing_type,
                                                  MinicType element_type,
                                                  const MinicArrayDeclaratorSyntax *declarator) {
    size_t dimension;

    if (!minic_sema_array_declarator_compatible_with_type(
            program, existing_type, element_type, declarator)) {
        return false;
    }

    for (dimension = 0U; dimension < declarator->dimension_count; ++dimension) {
        const MinicArrayType *existing_array;
        MinicType next_type;
        unsigned int bit;
        bool existing_incomplete;
        bool declared_incomplete;
        bool declared_zero_length;

        existing_array = minic_c0_program_array_type(program, existing_type.array_type_id);
        if (existing_array == NULL) {
            return false;
        }
        next_type = existing_array->element_type;
        bit = 1U << dimension;
        existing_incomplete =
            existing_array->element_count == 0U && !existing_array->is_zero_length;
        declared_incomplete = dimension == 0U && declarator->outermost_incomplete;
        declared_zero_length = (declarator->zero_length_mask & bit) != 0U;

        if (existing_incomplete && !declared_incomplete) {
            if (declared_zero_length) {
                if (!minic_c0_program_complete_zero_length_array_type(program, existing_type)) {
                    return false;
                }
            } else if (declarator->bounds[dimension] != 0U &&
                       !minic_c0_program_complete_array_type(
                           program, existing_type, declarator->bounds[dimension])) {
                return false;
            }
        }
        existing_type = next_type;
    }
    return true;
}

static inline bool
minic_sema_materialize_array_declarator(MinicC0Program *program,
                                        MinicType element_type,
                                        const MinicArrayDeclaratorSyntax *declarator,
                                        MinicType *result_type) {
    MinicType type;
    size_t dimension;

    if (program == NULL || declarator == NULL || result_type == NULL ||
        declarator->dimension_count == 0U ||
        declarator->dimension_count > MINIC_ARRAY_DECLARATOR_MAX_DIMENSIONS) {
        return false;
    }

    type = element_type;
    dimension = declarator->dimension_count;
    while (dimension > 0U) {
        unsigned int bit;

        dimension -= 1U;
        bit = 1U << dimension;
        if (dimension == 0U && declarator->outermost_incomplete) {
            if (!minic_c0_program_add_incomplete_array_type(program, type, &type)) {
                return false;
            }
        } else if ((declarator->zero_length_mask & bit) != 0U) {
            if (!minic_c0_program_add_zero_length_array_type(program, type, &type)) {
                return false;
            }
        } else if (!minic_c0_program_add_array_type(
                       program, type, declarator->bounds[dimension], &type)) {
            return false;
        }
    }

    *result_type = type;
    return true;
}

#endif
