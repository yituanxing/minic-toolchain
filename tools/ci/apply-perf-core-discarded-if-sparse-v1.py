#!/usr/bin/env python3
"""One-factor, opt-in Core discarded-IF reentry sparse membership experiment.

Run ONLY in a disposable CI checkout AFTER building the pristine baseline.
No checked-in production src/ changes. The existing whole-program membership
bitmap and eager incoming-edge index retain their original correctness role.
A bounded inline vector records distinct touched StatementIDs in traversal
order; on overflow/OOM use exactly the old dense scan. All nonindexed and
error paths remain fail-closed. Do not change statement_blocks initialization.
"""
from pathlib import Path

p = Path("src/core/core_lower.c")
source = p.read_text()


def once(old, new, label):
    global source
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"MINIC_CORE_SPARSE_PATCH=FAIL {label} occurrences={count}")
    source = source.replace(old, new, 1)


anchor = """static bool core_mark_discarded_if_membership(
    const MinicCoreLowerContext *context,
    const MinicBlock *block,
    bool *visited_blocks,
    bool *statement_membership) {"""

members = r"""/* CI experiment only: sparse keys do not replace the membership bitmap.
 * A zero-allocation inline collection fits most discarded IF subtrees.
 * Any allocation failure falls back to the original dense scan. */
typedef struct MinicCoreDiscardedSparseSet {
    size_t inline_ids[64];
    size_t *ids;
    size_t used;
    size_t capacity;
    bool overflow;
} MinicCoreDiscardedSparseSet;

static void minic_core_discarded_sparse_init(MinicCoreDiscardedSparseSet *set) {
    (void)memset(set, 0, sizeof(*set));
    set->ids = set->inline_ids;
    set->capacity = sizeof(set->inline_ids) / sizeof(set->inline_ids[0]);
}

static void minic_core_discarded_sparse_add(
    MinicCoreDiscardedSparseSet *set, size_t statement_id) {
    if (set == NULL || set->overflow) return;
    if (set->used == set->capacity) {
        size_t new_capacity = set->capacity * 2U;
        size_t *resized;
        if (new_capacity < set->capacity ||
            new_capacity > SIZE_MAX / sizeof(*set->ids)) {
            set->overflow = true;
            return;
        }
        if (set->ids == set->inline_ids) {
            resized = (size_t *)malloc(new_capacity * sizeof(*set->ids));
            if (resized != NULL) {
                (void)memcpy(resized, set->ids, set->used * sizeof(*set->ids));
            }
        } else {
            resized = (size_t *)realloc(
                set->ids, new_capacity * sizeof(*set->ids));
        }
        if (resized == NULL) {
            set->overflow = true;
            return;
        }
        set->ids = resized;
        set->capacity = new_capacity;
    }
    set->ids[set->used++] = statement_id;
}

static void minic_core_discarded_sparse_destroy(MinicCoreDiscardedSparseSet *set) {
    if (set->ids != set->inline_ids) free(set->ids);
    set->ids = NULL;
}

static bool core_mark_discarded_if_membership(
    const MinicCoreLowerContext *context,
    const MinicBlock *block,
    bool *visited_blocks,
    bool *statement_membership,
    MinicCoreDiscardedSparseSet *members) {"""
once(anchor, members, "unique definition")

once("""        statement_membership[statement_id] = true;
        statement = minic_c0_program_statement(program, statement_id);""",
     """        if (!statement_membership[statement_id]) {
            minic_core_discarded_sparse_add(members, statement_id);
        }
        statement_membership[statement_id] = true;
        statement = minic_c0_program_statement(program, statement_id);""",
     "list distinct statement IDs")

# The helper has two recursive callers (then and else), and the outer caller
# is replaced independently below. Both recursive targets are identical.
old = """                        context, child, visited_blocks, statement_membership)) {"""
if source.count(old) != 2:
    raise SystemExit(f"MINIC_CORE_SPARSE_PATCH=FAIL recursive callers={source.count(old)}")
source = source.replace(old,
    """                        context, child, visited_blocks, statement_membership, members)) {""")

once("""    bool unsafe;
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        block == NULL) {
        return true;
    }
    program = context->body->program;
    if (program->statement_count == 0U ||""",
     """    bool unsafe;
    size_t index;
    MinicCoreDiscardedSparseSet members = {0};
    bool use_sparse;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        block == NULL) {
        return true;
    }
    program = context->body->program;
    use_sparse = minic_core_reentry_index.program == program;
    if (use_sparse) minic_core_discarded_sparse_init(&members);
    if (program->statement_count == 0U ||""",
     "indexed path flag")

# If either early error path returns, release the sparse list initialized on
# entry (only the inline storage exists at this point).
once("""    if (program->statement_count == 0U ||
        program->block_count > SIZE_MAX / sizeof(*visited_blocks) ||
        program->statement_count > SIZE_MAX / sizeof(*statement_membership)) {
        return true;
    }
    visited_blocks = program->block_count == 0U""",
     """    if (program->statement_count == 0U ||
        program->block_count > SIZE_MAX / sizeof(*visited_blocks) ||
        program->statement_count > SIZE_MAX / sizeof(*statement_membership)) {
        return true;
    }
    visited_blocks = program->block_count == 0U""",
     "early guard proof")
# The above is an identity replacement, explicitly documenting that the
# early branch can only observe an inline (zero-heap) list.

once("""    unsafe = !core_mark_discarded_if_membership(
        context, block, visited_blocks, statement_membership);

    if (minic_core_reentry_index.program == program) {""",
     """    unsafe = !core_mark_discarded_if_membership(
        context, block, visited_blocks, statement_membership,
        use_sparse ? &members : NULL);

    if (minic_core_reentry_index.program == program) {""",
     "outer caller")

once("""        for (index = 0U; index < program->statement_count && !unsafe; ++index) {
            const MinicStatement *statement;
            size_t edge;
            if (!statement_membership[index]) continue;
            statement = minic_c0_program_statement(program, index);""",
     """        for (index = 0U;
             index < (members.overflow ? program->statement_count : members.used)
                 && !unsafe;
             ++index) {
            size_t statement_id = members.overflow ? index : members.ids[index];
            const MinicStatement *statement;
            size_t edge;
            if (!statement_membership[statement_id]) continue;
            statement = minic_c0_program_statement(program, statement_id);""",
     "indexed sparse target loop")

once("""                minic_core_reentry_index.addressed[index] != 0U) {""",
     """                minic_core_reentry_index.addressed[statement_id] != 0U) {""",
     "address taken sparse id")
once("""            for (edge = minic_core_reentry_index.heads[index]; edge != SIZE_MAX;
                 edge = minic_core_reentry_index.incoming[edge].next) {""",
     """            for (edge = minic_core_reentry_index.heads[statement_id];
                 edge != SIZE_MAX;
                 edge = minic_core_reentry_index.incoming[edge].next) {""",
     "incoming sparse id")

block_anchor = """    free(visited_blocks);
    free(statement_membership);
    return unsafe;
}


/* M192_IF_LOCAL_FACT_MEET */"""
once(block_anchor,
     """    if (use_sparse) minic_core_discarded_sparse_destroy(&members);
    free(visited_blocks);
    free(statement_membership);
    return unsafe;
}


/* M192_IF_LOCAL_FACT_MEET */""",
     "sparse cleanup")

# Preserve the lone short-circuit allocation failure cleanup unchanged. If
# inline capacity ever grows there, control still has not recorded members.
p.write_text(source)
print("MINIC_CORE_SPARSE_PATCH=APPLIED target=discarded_IF_reentry"
      " production_C_committed=false nonindexed_legacy_unchanged=true"
      " OOM_dense_fallback=true")
