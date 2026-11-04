#ifndef MINIC_COMPILER_INLINE_SPECIALIZE_COMMON_H
#define MINIC_COMPILER_INLINE_SPECIALIZE_COMMON_H

#include "frontend/ast.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Source-AST specialization facts, never CoreIR or machine ABI types. */
typedef struct MinicInlineSpecializationKey {
    bool integer_known[MINIC_MAX_FUNCTION_PARAMETERS];
    uint64_t integer_bits[MINIC_MAX_FUNCTION_PARAMETERS];
    bool symbolic_known[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicExpressionId symbolic_expression[MINIC_MAX_FUNCTION_PARAMETERS];
    bool boolean_range_known[MINIC_MAX_FUNCTION_PARAMETERS];
} MinicInlineSpecializationKey;

enum {
    MINIC_INLINE_SPEC_FACT_INTEGER = 1U,
    MINIC_INLINE_SPEC_FACT_SYMBOLIC = 2U,
    MINIC_INLINE_SPEC_FACT_BOOLEAN = 4U,
    MINIC_INLINE_SPEC_FACT_ALL = 7U
};

void minic_inline_spec_key_clear(MinicInlineSpecializationKey *key);
bool minic_inline_spec_key_from_function(
    MinicInlineSpecializationKey *key, const MinicFunction *function,
    size_t parameter_count);
bool minic_inline_spec_section_matches(
    const MinicFunction *candidate, const MinicFunction *source,
    const MinicFunction *caller);
bool minic_inline_spec_sections_present(const MinicC0Program *program);
bool minic_inline_spec_key_matches(
    const MinicFunction *candidate, const MinicFunction *source,
    const MinicFunction *caller, MinicFunctionId source_id,
    const MinicInlineSpecializationKey *key, unsigned int fact_mask,
    size_t parameter_count);
MinicFunctionId minic_inline_spec_find_variant(
    const MinicC0Program *program, const MinicFunction *source,
    const MinicFunction *caller, MinicFunctionId source_id,
    const MinicInlineSpecializationKey *key, unsigned int fact_mask,
    size_t first_index);

/* The original callsite->canonical source owner is mandatory when sections
 * exist. Unknown or ambiguous owners remain MINIC_FUNCTION_INVALID / >=
 * function_limit and are never guessed from a specialized clone. */
typedef struct MinicInlineInitialOwners {
    MinicFunctionId *owners;
    size_t expression_count;
    size_t function_limit;
    bool ready;
} MinicInlineInitialOwners;

bool minic_inline_initial_owners_build(
    const MinicC0Program *program, size_t function_limit,
    MinicInlineInitialOwners *owners);
MinicFunctionId minic_inline_initial_owners_get(
    const MinicInlineInitialOwners *owners, size_t expression_index);
void minic_inline_initial_owners_destroy(MinicInlineInitialOwners *owners);

/* Optional transitive CALL accelerator. The stable prefix is grouped by
 * canonical source owner; ambiguous/unowned CALLs use a common bucket. Every
 * cursor merges both buckets by ascending ExpressionId, then observes all
 * expressions appended after the snapshot. An unavailable index transparently
 * scans the entire live expression arena. */
typedef struct MinicInlineCallWorklist {
    const MinicC0Program *program;
    size_t snapshot_expression_count;
    size_t source_function_limit;
    size_t call_count;
    size_t valid_call_count;
    size_t *offsets;
    size_t *calls;
    size_t *valid_counts;
    bool owner_ready;
    bool ready;
} MinicInlineCallWorklist;

typedef struct MinicInlineCallCursor {
    const MinicInlineCallWorklist *worklist;
    size_t own_cursor, own_end;
    size_t common_cursor, common_end;
    size_t late_cursor, full_cursor;
    size_t skipped_other_owner;
} MinicInlineCallCursor;

bool minic_inline_call_worklist_build(
    const MinicC0Program *program, size_t source_function_limit,
    MinicInlineCallWorklist *worklist);
void minic_inline_call_worklist_destroy(MinicInlineCallWorklist *worklist);
void minic_inline_call_cursor_begin(
    const MinicInlineCallWorklist *worklist,
    MinicFunctionId canonical_source_id, MinicInlineCallCursor *cursor);
bool minic_inline_call_cursor_next(
    MinicInlineCallCursor *cursor, const MinicC0Program *program,
    MinicExpressionId *expression_id);

#endif
