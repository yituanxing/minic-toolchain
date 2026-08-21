#ifndef MINIC_FRONTEND_SEMA_H
#define MINIC_FRONTEND_SEMA_H

#include "frontend/ast.h"

#include <stdbool.h>
#include <stddef.h>

#define MINIC_ARRAY_DECLARATOR_MAX_DIMENSIONS 8U

typedef struct MinicArrayDeclaratorSyntax {
    size_t bounds[MINIC_ARRAY_DECLARATOR_MAX_DIMENSIONS];
    size_t dimension_count;
    unsigned int zero_length_mask;
    bool outermost_incomplete;
} MinicArrayDeclaratorSyntax;

/*
 * Array declarator syntax is intentionally transient. Parsing fills this value
 * without growing Program-owned type arenas; semantic code materializes the
 * canonical semantic type only at an explicit commit point.
 *
 * This is the first Declaration/Sema transaction seam. Keep target, ABI, OS,
 * and object-format facts out of this representation.
 */
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
