#ifndef MINIC_TARGET_COMMON_CORE_CALL_VIEW_H
#define MINIC_TARGET_COMMON_CORE_CALL_VIEW_H

#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>

/* Source/Core call facts, not an ABI classifier or machine call plan.
 * A single checked resolver feeds SysV, RV64, AAPCS64 and LP64D paths.
 * No register/stack or aggregate-passing decision belongs here. */
typedef struct MinicCoreCallView {
    const MinicCoreCallee *callee;             /* direct, otherwise NULL */
    const MinicCoreCallSignature *signature;   /* indirect, otherwise NULL */
    const MinicCoreCallArgument *arguments;    /* NULL when count == 0 */
    const MinicType *parameter_types;
    MinicType return_type;
    MinicCoreValueId function_pointer;         /* indirect only */
    size_t argument_begin;
    size_t argument_count;
    size_t parameter_count;
    bool is_variadic;
} MinicCoreCallView;

/* Validates signature provenance, function-pointer type, argument extent,
 * arity and non-null backing storage without imposing any psABI.
 * The caller still validates operand kinds/types and ABI legality. */
bool minic_core_call_view_build(const MinicCoreFunction *,
                                const MinicCoreInstruction *,
                                MinicCoreCallView *);

/* The actual Core value/object type. Declared fixed-parameter types are
 * separate: psABI-specific type matching remains with target validation. */
bool minic_core_call_argument_actual_type(const MinicCoreFunction *,
                                           const MinicCoreCallArgument *,
                                           MinicType *);

#endif
