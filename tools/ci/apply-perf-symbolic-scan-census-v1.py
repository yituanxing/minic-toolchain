#!/usr/bin/env python3
"""Opt-in, count-only Symbolic Specialization census on CURRENT production C.

Apply after the existing specialization-phase and symbolic direct/transitive
tracers in a disposable CI checkout. Never apply to checked-in src.
Counters measure algorithmic operations, NOT cycles, wall-time speedups or RAM
traffic. Leave clone selection, ExpressionId traversal and error exits intact.
"""
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()
start = text.index("static bool minic_specialize_inline_symbolic_calls(\n")
end = text.index("\nstatic bool minic_inline_boolean_domain_value(", start)
block = text[start:end]

def once(old, new, label):
    global block
    hits = block.count(old)
    if hits != 1:
        raise SystemExit(f"MINIC_SYMBOLIC_CENSUS_ANCHOR=FAIL {label} hits={hits}")
    block = block.replace(old, new, 1)

once("""    size_t expression_index;
    size_t initial_clone_count = 0U;""",
"""    size_t expression_index;
    unsigned long long direct_expression_visits = 0ULL;
    unsigned long long direct_call_visits = 0ULL;
    unsigned long long direct_variant_lookups = 0ULL;
    unsigned long long direct_variant_probes = 0ULL;
    unsigned long long transitive_callers = 0ULL;
    unsigned long long transitive_expression_visits = 0ULL;
    unsigned long long transitive_owner_skips = 0ULL;
    unsigned long long transitive_call_visits = 0ULL;
    unsigned long long transitive_pointer_argument_evals = 0ULL;
    unsigned long long transitive_variant_lookups = 0ULL;
    unsigned long long transitive_variant_probes = 0ULL;
    unsigned long long owner_index_ready = 0ULL;
    size_t initial_clone_count = 0U;""", "counters")

once("""        MinicExpression *call = &program->expressions[expression_index];
        MinicFunctionId current_id;""",
"""        MinicExpression *call = &program->expressions[expression_index];
        ++direct_expression_visits;
        if (call->kind == MINIC_EXPRESSION_CALL) {
            ++direct_call_visits;
        }
        MinicFunctionId current_id;""", "direct-visits")

once("""        for (candidate_index = 0U; candidate_index < program->function_count;
             ++candidate_index) {
            if (minic_inline_symbolic_variant_matches(""",
"""        ++direct_variant_lookups;
        for (candidate_index = 0U; candidate_index < program->function_count;
             ++candidate_index) {
            ++direct_variant_probes;
            if (minic_inline_symbolic_variant_matches(""", "direct-variant")

once("""        if (!symbolic_owner_index_ready) {
            free(symbolic_expression_owners);
            symbolic_expression_owners = NULL;
        }
        (void)original_function_count;""",
"""        if (!symbolic_owner_index_ready) {
            free(symbolic_expression_owners);
            symbolic_expression_owners = NULL;
        }
        owner_index_ready = symbolic_owner_index_ready ? 1ULL : 0ULL;
        (void)original_function_count;""", "owner-index")

once("""            if (!caller.is_integer_specialization ||
                caller.specialization_source >= program->function_count) {
                continue;
            }
            for (nested_expression_index = 0U;""",
"""            if (!caller.is_integer_specialization ||
                caller.specialization_source >= program->function_count) {
                continue;
            }
            ++transitive_callers;
            for (nested_expression_index = 0U;""", "transitive-callers")

once("""                const MinicExpression *nested =
                    &program->expressions[nested_expression_index];
                MinicFunctionId current_id;""",
"""                const MinicExpression *nested =
                    &program->expressions[nested_expression_index];
                MinicFunctionId current_id;
                ++transitive_expression_visits;""", "transitive-visits")

once("""                    if (nested_owner != MINIC_FUNCTION_INVALID &&
                        nested_owner < original_function_count &&
                        nested_owner != caller.specialization_source) {
                        continue;
                    }""",
"""                    if (nested_owner != MINIC_FUNCTION_INVALID &&
                        nested_owner < original_function_count &&
                        nested_owner != caller.specialization_source) {
                        ++transitive_owner_skips;
                        continue;
                    }""", "owner-skips")

once("""                if (nested->kind != MINIC_EXPRESSION_CALL ||
                    nested->value.call.function_id == MINIC_FUNCTION_INVALID) {
                    continue;
                }""",
"""                if (nested->kind == MINIC_EXPRESSION_CALL) {
                    ++transitive_call_visits;
                }
                if (nested->kind != MINIC_EXPRESSION_CALL ||
                    nested->value.call.function_id == MINIC_FUNCTION_INVALID) {
                    continue;
                }""", "transitive-calls")

once("""                for (i = 0U; i < source.parameter_count; ++i) {
                    MinicExpressionId resolved_expression;
                    bool argument_used_caller_fact = false;
                    if (!minic_type_is_pointer(source.parameter_types[i]) ||""",
"""                for (i = 0U; i < source.parameter_count; ++i) {
                    MinicExpressionId resolved_expression;
                    bool argument_used_caller_fact = false;
                    if (minic_type_is_pointer(source.parameter_types[i])) {
                        ++transitive_pointer_argument_evals;
                    }
                    if (!minic_type_is_pointer(source.parameter_types[i]) ||""", "pointer-argument-evals")

once("""                for (candidate_index = 0U;
                     candidate_index < program->function_count;
                     ++candidate_index) {
                    if (minic_inline_symbolic_variant_matches(""",
"""                ++transitive_variant_lookups;
                for (candidate_index = 0U;
                     candidate_index < program->function_count;
                     ++candidate_index) {
                    ++transitive_variant_probes;
                    if (minic_inline_symbolic_variant_matches(""", "transitive-variant")

once("""    if (initial_clone_count != 0U || transitive_clone_count != 0U) {""",
"""    if (getenv("MINIC_SYMBOLIC_SCAN_PROFILE") != NULL) {
        (void)fprintf(stderr,
                      "MINIC_SYMBOLIC_SCAN_PROFILE direct_expression_visits=%llu "
                      "direct_call_visits=%llu direct_variant_lookups=%llu "
                      "direct_variant_probes=%llu transitive_callers=%llu "
                      "transitive_expression_visits=%llu transitive_owner_skips=%llu "
                      "transitive_call_visits=%llu transitive_pointer_argument_evals=%llu "
                      "transitive_variant_lookups=%llu transitive_variant_probes=%llu "
                      "owner_index_ready=%llu direct_clones=%zu transitive_clones=%zu\\n",
                      direct_expression_visits, direct_call_visits,
                      direct_variant_lookups, direct_variant_probes,
                      transitive_callers, transitive_expression_visits,
                      transitive_owner_skips, transitive_call_visits,
                      transitive_pointer_argument_evals, transitive_variant_lookups,
                      transitive_variant_probes, owner_index_ready,
                      initial_clone_count, transitive_clone_count);
    }
    if (initial_clone_count != 0U || transitive_clone_count != 0U) {""", "one-report")

p.write_text(text[:start] + block + text[end:])
print("MINIC_SYMBOLIC_SCAN_CENSUS_PATCH=APPLIED diagnostic_only=true")
