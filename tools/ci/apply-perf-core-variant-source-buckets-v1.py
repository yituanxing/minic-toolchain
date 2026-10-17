#!/usr/bin/env python3
"""Bench-only Core Lower three-selector source ID buckets, no production edit.

Old cost: each direct CALL runs three separate full function_count loops
even though most functions are unrelated. Build ascending function-ID chains
grouped by stable specialization_source, scoped to one Core Function Set.
OOM, unknown source, or standalone Core lowering use EXACT legacy full-scan.
The opt-in injected fault hook tests the old fallback, not production.
"""
from pathlib import Path

p=Path("src/core/core_lower.c")
s=p.read_text()

def replace_once(source, before, after, label):
    count=source.count(before)
    if count!=1:
        raise SystemExit(f"MINIC_CORE_VARIANT_SOURCE_BUCKET=FAIL {label} anchors={count}")
    return source.replace(before,after,1)

anchor="static MinicCoreReentryIndex minic_core_reentry_index;\n"
impl=r'''
/* A Core Function Set is read-only and the specialized variants retain
 * stable Function IDs.  Each source bucket is in increasing Function-ID
 * order, preserving 'first compatible best fact count' tie selection.
 * Only publish a fully allocated index; every other case falls back.
 * Unlike the rejected no-facts shortcut, this reduces the actual number
 * of unrelated variant visits even when argument facts ARE present. */
typedef struct MinicCoreVariantSourceIndex {
    const MinicC0Program *program;
    size_t function_count;
    size_t *heads;
    size_t *tails;
    size_t *next;
} MinicCoreVariantSourceIndex;

static MinicCoreVariantSourceIndex minic_core_variant_index;

static void minic_core_variant_index_end(void) {
    free(minic_core_variant_index.heads);
    free(minic_core_variant_index.tails);
    free(minic_core_variant_index.next);
    (void)memset(&minic_core_variant_index, 0, sizeof(minic_core_variant_index));
}

static void minic_core_variant_index_begin(const MinicC0Program *program) {
    MinicCoreVariantSourceIndex *index = &minic_core_variant_index;
    size_t n, i;
    minic_core_variant_index_end();
    if (program == NULL || program->functions == NULL || program->function_count == 0U ||
        program->function_count > SIZE_MAX / sizeof(*index->heads) ||
        getenv("MINIC_TEST_FORCE_CORE_VARIANT_INDEX_FALLBACK") != NULL) {
        return;
    }
    n = program->function_count;
    index->heads = (size_t *)malloc(n * sizeof(*index->heads));
    index->tails = (size_t *)malloc(n * sizeof(*index->tails));
    index->next = (size_t *)malloc(n * sizeof(*index->next));
    if (index->heads == NULL || index->tails == NULL || index->next == NULL) {
        minic_core_variant_index_end();
        return;
    }
    for (i = 0U; i < n; ++i) {
        index->heads[i] = SIZE_MAX;
        index->tails[i] = SIZE_MAX;
        index->next[i] = SIZE_MAX;
    }
    for (i = 0U; i < n; ++i) {
        const MinicFunction *candidate = &program->functions[i];
        size_t owner;
        if (!candidate->is_integer_specialization) continue;
        owner = candidate->specialization_source;
        if (owner >= n) {
            /* Malformed semantic ownership: fail closed to full scan. */
            minic_core_variant_index_end();
            return;
        }
        if (index->heads[owner] == SIZE_MAX) {
            index->heads[owner] = i;
        } else {
            index->next[index->tails[owner]] = i;
        }
        index->tails[owner] = i;
    }
    index->function_count = n;
    index->program = program; /* Publish only once complete. */
}

static bool minic_core_variant_index_matches(const MinicC0Program *program) {
    return minic_core_variant_index.program == program &&
           minic_core_variant_index.function_count == program->function_count;
}
'''
s=replace_once(s,anchor,anchor+impl,"type/index")
s=replace_once(s,"void minic_core_lower_reentry_index_end(void) {\n",
               "void minic_core_lower_reentry_index_end(void) {\n    minic_core_variant_index_end();\n","end")
s=replace_once(s, "    minic_core_lower_reentry_index_end();\n    if (program == NULL || program->statement_count == 0U ||",
               "    minic_core_lower_reentry_index_end();\n    minic_core_variant_index_begin(program);\n    if (program == NULL || program->statement_count == 0U ||","begin")

def update_selector(text,start_label,end_label,old_loop):
    start=text.index(start_label)
    end=text.index(end_label,start)
    original=text[start:end]
    declaration="    size_t candidate_index;\n"
    if original.count(declaration)!=1:
        raise SystemExit("selector candidate index declaration drift: "+start_label)
    original=original.replace(declaration,
        declaration+"    bool use_variant_source_index;\n",1)
    current=(
        "    for (candidate_index = 0U; candidate_index < program->function_count;"
        + ("\n         ++candidate_index)" if old_loop=="wrapped" else " ++candidate_index)")
        + " {"
    )
    if original.count(current)!=1:
        raise SystemExit("candidate loop changed: "+start_label+" type="+old_loop)
    replacement='''    use_variant_source_index = minic_core_variant_index_matches(program);
    for (candidate_index = use_variant_source_index
                               ? minic_core_variant_index.heads[source_id]
                               : 0U;
         candidate_index < program->function_count;
         candidate_index = use_variant_source_index
                               ? minic_core_variant_index.next[candidate_index]
                               : candidate_index + 1U) {'''
    original=original.replace(current,replacement,1)
    return text[:start]+original+text[end:]

s=update_selector(s,"static const MinicFunction *core_select_transitive_integer_specialization(",
                  "static bool core_inline_symbolic_argument_expression(","inline")
s=update_selector(s,"static const MinicFunction *core_select_transitive_symbolic_specialization(",
                  "static bool core_boolean_range_specialization_same_base_facts(","wrapped")
s=update_selector(s,"static const MinicFunction *core_select_boolean_range_specialization(",
                  "static MinicCoreLowerStatus lower_direct_call(","wrapped")
p.write_text(s)
print("MINIC_CORE_VARIANT_SOURCE_BUCKET=APPLIED three_selectors=true OOM_fallback=true stable_order=true")
