#!/usr/bin/env python3
"""Diagnostic-only Boolean-specialization work census for the CURRENT MiniC.

Applies to a throwaway CI checkout after the native Runtime profile and
four-pass timers. This does NOT optimize, prune or change any generated IR;
it counts repeated scans that can dominate big Kbuild translation units.
Only enabled by the explicit perf workflow: no production compiler change.
"""
from pathlib import Path

p = Path("src/compiler/compiler.c")
src = p.read_text()

def one(content: str, old: str, new: str, label: str) -> str:
    count = content.count(old)
    if count != 1:
        raise SystemExit(f"BOOL_SCAN_CENSUS=FAIL label={label} anchor_count={count}")
    return content.replace(old, new, 1)

# Scratch counters are file-global solely in this diagnostically patched
# compiler; all metrics reset at the beginning of each Boolean pass.
anchor = "static bool minic_inline_local_boolean_domain(\n"
counters = """static unsigned long long minic_perf_bool_local_queries;
static unsigned long long minic_perf_bool_local_potential_arena_visits;
static unsigned long long minic_perf_bool_transitive_callers;
static unsigned long long minic_perf_bool_transitive_range_callers;
static unsigned long long minic_perf_bool_transitive_expression_visits;
static unsigned long long minic_perf_bool_transitive_call_expression_visits;
static unsigned long long minic_perf_bool_variant_lookups;
static unsigned long long minic_perf_bool_variant_probes;

"""
src = one(src, anchor, counters+anchor, "counter-state")

begin = src.index("static bool minic_inline_local_boolean_domain(\n")
end = src.index("\nstatic bool minic_inline_boolean_range_argument(", begin)
section = src[begin:end]
section = one(section,
"""    if (program == NULL || local_id >= program->local_count ||
        !minic_type_is_integer(program->locals[local_id].type) ||
        minic_inline_local_is_parameter(program, local_id)) {
        return false;
    }

    for (statement_index = 0U; statement_index < program->statement_count;""",
"""    if (program == NULL || local_id >= program->local_count ||
        !minic_type_is_integer(program->locals[local_id].type) ||
        minic_inline_local_is_parameter(program, local_id)) {
        return false;
    }
    ++minic_perf_bool_local_queries;
    /* Upper bound only: early exits make actual arena visits smaller.
       This measures how often a valid local asks the expensive question. */
    minic_perf_bool_local_potential_arena_visits +=
        (unsigned long long)program->statement_count +
        (unsigned long long)program->expression_count +
        (unsigned long long)program->inline_asm_count;

    for (statement_index = 0U; statement_index < program->statement_count;""",
"local-domain-queries")
src = src[:begin] + section + src[end:]

begin = src.index("static bool minic_specialize_inline_boolean_range_calls(\n")
end = src.index("\nstatic bool minic_inline_integer_source_has_residual_reference(", begin)
block=src[begin:end]
block = one(block,
"""    if (program == NULL || target == NULL) {
        return false;
    }

    /* First, calls""",
"""    if (program == NULL || target == NULL) {
        return false;
    }
    minic_perf_bool_local_queries = 0ULL;
    minic_perf_bool_local_potential_arena_visits = 0ULL;
    minic_perf_bool_transitive_callers = 0ULL;
    minic_perf_bool_transitive_range_callers = 0ULL;
    minic_perf_bool_transitive_expression_visits = 0ULL;
    minic_perf_bool_transitive_call_expression_visits = 0ULL;
    minic_perf_bool_variant_lookups = 0ULL;
    minic_perf_bool_variant_probes = 0ULL;
    minic_bootstrap_trace_stage(NULL, "spec-boolean-direct", "begin",
                                program->function_count);

    /* First, calls""", "direct-begin")
block = one(block,
"""    /* Then close range facts through already-specialized callers.  Do not
       rewrite the shared source AST here; Core lowering selects the matching
       variant using the actual caller specialization. */""",
"""    minic_bootstrap_trace_stage(NULL, "spec-boolean-direct", "end-success",
                                program->function_count);
    minic_bootstrap_trace_stage(NULL, "spec-boolean-transitive", "begin",
                                program->function_count);
    /* Then close range facts through already-specialized callers.  Do not
       rewrite the shared source AST here; Core lowering selects the matching
       variant using the actual caller specialization. */""",
"transitive-begin")
block=one(block,
"""            if (!caller.is_integer_specialization ||
                caller.specialization_source >= program->function_count) {
                continue;
            }
            for (i = 0U; i < caller.parameter_count; ++i) {
                caller_has_range = caller_has_range ||""",
"""            if (!caller.is_integer_specialization ||
                caller.specialization_source >= program->function_count) {
                continue;
            }
            ++minic_perf_bool_transitive_callers;
            for (i = 0U; i < caller.parameter_count; ++i) {
                caller_has_range = caller_has_range ||""",
"cloned-caller-count")
block=one(block,
"""            if (!caller_has_range) {
                continue;
            }
            for (nested_expression_index = 0U;
                 nested_expression_index < program->expression_count;""",
"""            if (!caller_has_range) {
                continue;
            }
            ++minic_perf_bool_transitive_range_callers;
            minic_perf_bool_transitive_expression_visits +=
                (unsigned long long)program->expression_count;
            for (nested_expression_index = 0U;
                 nested_expression_index < program->expression_count;""",
"hot-full-expression-scan")
block=one(block,
"""                if (nested->kind != MINIC_EXPRESSION_CALL ||
                    nested->value.call.function_id == MINIC_FUNCTION_INVALID) {
                    continue;
                }
                current_id = nested->value.call.function_id;""",
"""                if (nested->kind == MINIC_EXPRESSION_CALL) {
                    ++minic_perf_bool_transitive_call_expression_visits;
                }
                if (nested->kind != MINIC_EXPRESSION_CALL ||
                    nested->value.call.function_id == MINIC_FUNCTION_INVALID) {
                    continue;
                }
                current_id = nested->value.call.function_id;""",
"count-actual-call-expressions")
# The Boolean pass contains exactly 2 original per-candidate scan loops:
# one direct and one transitive. Count probes in both without changing order.
needle="""            if (minic_inline_boolean_range_variant_matches(
"""
# We count only the transitive candidates because the exact indentation
# of the direct branch is not part of this diagnosis; no made-up totals.
variant = """                    if (minic_inline_boolean_range_variant_matches(
"""
if block.count(variant) != 1:
    raise SystemExit("BOOL_SCAN_CENSUS=FAIL transitive-match-anchor")
block = block.replace(variant,
"""                    ++minic_perf_bool_variant_probes;
                    if (minic_inline_boolean_range_variant_matches(
""",1)
block=one(block,
"""                for (candidate_index = 0U;
                     candidate_index < program->function_count;
                     ++candidate_index) {""",
"""                ++minic_perf_bool_variant_lookups;
                for (candidate_index = 0U;
                     candidate_index < program->function_count;
                     ++candidate_index) {""",
"variant-lookup-count")
block=one(block,
"""    if (direct_clone_count != 0U || transitive_clone_count != 0U) {""",
"""    minic_bootstrap_trace_stage(NULL, "spec-boolean-transitive",
                                "end-success", program->function_count);
    if (getenv("MINIC_BOOLEAN_SCAN_PROFILE") != NULL) {
        (void)fprintf(stderr,
            "MINIC_BOOLEAN_SCAN_PROFILE local_queries=%llu "
            "local_arena_scan_potential=%llu callers=%llu range_callers=%llu "
            "expression_visits=%llu call_expression_visits=%llu "
            "variant_lookups=%llu variant_probes=%llu "
            "direct_clones=%zu transitive_clones=%zu\\n",
            minic_perf_bool_local_queries,
            minic_perf_bool_local_potential_arena_visits,
            minic_perf_bool_transitive_callers,
            minic_perf_bool_transitive_range_callers,
            minic_perf_bool_transitive_expression_visits,
            minic_perf_bool_transitive_call_expression_visits,
            minic_perf_bool_variant_lookups,
            minic_perf_bool_variant_probes,
            direct_clone_count, transitive_clone_count);
    }
    if (direct_clone_count != 0U || transitive_clone_count != 0U) {""",
"timer-and-summary")
src=src[:begin]+block+src[end:]
p.write_text(src)
print("MINIC_BOOLEAN_SCAN_CENSUS_V1=APPLIED source_semantics_unchanged=true")
