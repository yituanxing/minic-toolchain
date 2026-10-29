#ifndef MINIC_TARGET_COMMON_CORE_CALL_VIEW_H
#define MINIC_TARGET_COMMON_CORE_CALL_VIEW_H

#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>

/* Source/Core call facts, not an ABI classifier or machine call plan.
 * A single checked resolver feeds SysV, RV64, AAPCS64 and LP64D paths.
 * No register/stack or aggregate-passing decision belongs here. */
typedef enum MinicCoreCallReturnForm {
    MINIC_CORE_CALL_RETURN_VOID = 0,
    MINIC_CORE_CALL_RETURN_VALUE,
    MINIC_CORE_CALL_RETURN_OBJECT
} MinicCoreCallReturnForm;

/* Source result storage, NOT ABI return register classification. */
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
    MinicFunctionCallingConvention calling_convention;
    MinicCoreCallReturnForm return_form;
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
/* Common checked argument/record identity, all five backend call planners. */
bool minic_core_call_view_argument_type(const MinicCoreFunction *,
                                        const MinicCoreCallView *, size_t,
                                        MinicType *);

/* Once-per-definition, machine-independent CALL semantic index. Only real
 * CALL IDs are stored, in ascending order; no per-instruction pointer table.
 * Each view borrows immutable Core IR. Target ABI placement remains separate. */
typedef struct MinicCoreCallSite {
    size_t instruction_id;
    MinicCoreCallView view;
} MinicCoreCallSite;
typedef struct MinicCoreCallIndex {
    MinicCoreCallSite *sites;
    size_t count;
} MinicCoreCallIndex;
bool minic_core_call_index_build(const MinicCoreFunction *, size_t expected_calls,
                                 MinicCoreCallIndex *);
/* One semantic lookup yields the stable ordinal of every target ABI cache. */
const MinicCoreCallView *minic_core_call_index_find(
    const MinicCoreCallIndex *, size_t instruction_id, size_t *ordinal);
void minic_core_call_index_destroy(MinicCoreCallIndex *);

/* A single owner for production's borrowed semantic CALL index and the
 * identical standalone fallback index. ABI caches never rescan Core or
 * implement an independent sparse CALL lookup. */
typedef struct MinicCoreCallIndexHandle {
    MinicCoreCallIndex owned;
    const MinicCoreCallIndex *index;
} MinicCoreCallIndexHandle;
bool minic_core_call_index_handle_prepare(
    const MinicCoreFunction *, const MinicCoreCallIndex *,
    MinicCoreCallIndexHandle *);
void minic_core_call_index_handle_destroy(MinicCoreCallIndexHandle *);

#endif
