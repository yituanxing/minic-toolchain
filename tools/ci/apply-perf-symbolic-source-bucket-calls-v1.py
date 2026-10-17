#!/usr/bin/env python3
"""Disposable one-factor A/B: source-grouped Symbolic transitive CALL visits.

The production compiler.c stays unmodified in Git. This patch is applied only
after an authenticated baseline compiler is built in the existing perf500 job.
Unlike rejected PERF-023's global CALL-only vector, it builds source-specific
ascending CALL ID buckets plus a conservative unknown/shared bucket.

New expressions appended during transitive specialization remain on the legacy
dynamic tail scan. Index OOM/validation failure keeps the exact old full scan.
"""
from pathlib import Path

p = Path("src/compiler/compiler.c")
src = p.read_text()
start = src.index("static bool minic_specialize_inline_symbolic_calls(\n")
end = src.index("\nstatic bool minic_inline_boolean_domain_value(", start)
block = src[start:end]

def once(old: str, new: str, label: str):
    global block
    n = block.count(old)
    if n != 1:
        raise SystemExit(f"MINIC_SYMBOLIC_BUCKET_PATCH=FAIL label={label} anchors={n}")
    block = block.replace(old, new, 1)

once("""        (void)original_function_count;
        for (caller_index = 0U;""",
"""        /* Indexed CALLs are grouped by their *canonical* source owner.
         * Shared, ambiguous and orphan CALLs are retained in the common bucket.
         * Retain ascending ExpressionId order using a two-way merge.
         * All storage is optional; any OOM falls back to the original scan.
         * Late appends are scanned separately using the live expression_count. */
        size_t *symbolic_source_bucket_offsets = NULL;
        size_t *symbolic_source_bucket_calls = NULL;
        size_t *symbolic_source_bucket_writes = NULL;
        bool symbolic_source_buckets_ready = false;
        if (symbolic_owner_index_ready &&
            symbolic_owned_expression_count != 0U &&
            original_function_count <= SIZE_MAX / sizeof(size_t) - 2U &&
            symbolic_owned_expression_count <= SIZE_MAX / sizeof(size_t)) {
            size_t n = original_function_count;
            size_t idx;
            symbolic_source_bucket_offsets = (size_t *)calloc(
                n + 2U, sizeof(*symbolic_source_bucket_offsets));
            symbolic_source_bucket_calls = (size_t *)malloc(
                symbolic_owned_expression_count * sizeof(*symbolic_source_bucket_calls));
            symbolic_source_bucket_writes = (size_t *)malloc(
                (n + 1U) * sizeof(*symbolic_source_bucket_writes));
            if (symbolic_source_bucket_offsets != NULL &&
                symbolic_source_bucket_calls != NULL &&
                symbolic_source_bucket_writes != NULL) {
                for (idx = 0U; idx < symbolic_owned_expression_count; ++idx) {
                    if (program->expressions[idx].kind == MINIC_EXPRESSION_CALL) {
                        MinicFunctionId owner = symbolic_expression_owners[idx];
                        size_t bucket = (owner != MINIC_FUNCTION_INVALID && owner < n)
                                            ? (size_t)owner : n;
                        ++symbolic_source_bucket_offsets[bucket + 1U];
                    }
                }
                for (idx = 0U; idx < n + 1U; ++idx) {
                    symbolic_source_bucket_offsets[idx + 1U] +=
                        symbolic_source_bucket_offsets[idx];
                }
                (void)memcpy(symbolic_source_bucket_writes,
                             symbolic_source_bucket_offsets,
                             (n + 1U) * sizeof(*symbolic_source_bucket_writes));
                for (idx = 0U; idx < symbolic_owned_expression_count; ++idx) {
                    if (program->expressions[idx].kind == MINIC_EXPRESSION_CALL) {
                        MinicFunctionId owner = symbolic_expression_owners[idx];
                        size_t bucket = (owner != MINIC_FUNCTION_INVALID && owner < n)
                                            ? (size_t)owner : n;
                        symbolic_source_bucket_calls[
                            symbolic_source_bucket_writes[bucket]++] = idx;
                    }
                }
                symbolic_source_buckets_ready = true;
            }
        }
        free(symbolic_source_bucket_writes);
        if (!symbolic_source_buckets_ready) {
            free(symbolic_source_bucket_offsets);
            free(symbolic_source_bucket_calls);
            symbolic_source_bucket_offsets = NULL;
            symbolic_source_bucket_calls = NULL;
        }
        (void)original_function_count;
        for (caller_index = 0U;""", "index-build")

once("""            size_t nested_expression_index;

            if (!caller.is_integer_specialization ||
                caller.specialization_source >= program->function_count) {
                continue;
            }
            for (nested_expression_index = 0U;
                 nested_expression_index < program->expression_count;
                 ++nested_expression_index) {
                const MinicExpression *nested =
                    &program->expressions[nested_expression_index];""",
"""            size_t symbolic_full_cursor = 0U;
            size_t symbolic_own_cursor = 0U;
            size_t symbolic_own_end = 0U;
            size_t symbolic_common_cursor = 0U;
            size_t symbolic_common_end = 0U;
            size_t symbolic_late_cursor = symbolic_owned_expression_count;

            if (!caller.is_integer_specialization ||
                caller.specialization_source >= program->function_count) {
                continue;
            }
            if (symbolic_source_buckets_ready) {
                size_t n = original_function_count;
                if (caller.specialization_source < n) {
                    symbolic_own_cursor =
                        symbolic_source_bucket_offsets[caller.specialization_source];
                    symbolic_own_end =
                        symbolic_source_bucket_offsets[caller.specialization_source + 1U];
                }
                symbolic_common_cursor = symbolic_source_bucket_offsets[n];
                symbolic_common_end = symbolic_source_bucket_offsets[n + 1U];
            }
            for (;;) {
                size_t nested_expression_index;
                if (!symbolic_source_buckets_ready) {
                    if (symbolic_full_cursor >= program->expression_count) {
                        break;
                    }
                    nested_expression_index = symbolic_full_cursor++;
                } else if (symbolic_own_cursor < symbolic_own_end ||
                           symbolic_common_cursor < symbolic_common_end) {
                    if (symbolic_common_cursor == symbolic_common_end ||
                        (symbolic_own_cursor < symbolic_own_end &&
                         symbolic_source_bucket_calls[symbolic_own_cursor] <
                             symbolic_source_bucket_calls[symbolic_common_cursor])) {
                        nested_expression_index =
                            symbolic_source_bucket_calls[symbolic_own_cursor++];
                    } else {
                        nested_expression_index =
                            symbolic_source_bucket_calls[symbolic_common_cursor++];
                    }
                } else if (symbolic_late_cursor < program->expression_count) {
                    /* The old loop observes append-only expressions, including
                     * ones created while processing this same caller. */
                    nested_expression_index = symbolic_late_cursor++;
                } else {
                    break;
                }
                const MinicExpression *nested =
                    &program->expressions[nested_expression_index];""", "scan-loop")

once("""                        free(symbolic_expression_owners);
                        return false;
                    }
                    transitive_clone_count += 1U;""",
"""                        free(symbolic_source_bucket_offsets);
                        free(symbolic_source_bucket_calls);
                        free(symbolic_expression_owners);
                        return false;
                    }
                    transitive_clone_count += 1U;""", "failure-cleanup")

once("""        free(symbolic_expression_owners);
    }

    if (initial_clone_count != 0U || transitive_clone_count != 0U) {""",
"""        free(symbolic_source_bucket_offsets);
        free(symbolic_source_bucket_calls);
        free(symbolic_expression_owners);
    }

    if (initial_clone_count != 0U || transitive_clone_count != 0U) {""", "normal-cleanup")

p.write_text(src[:start] + block + src[end:])
print("MINIC_SYMBOLIC_SOURCE_BUCKET_CALLS_V1=APPLIED opt_in_only=true")
