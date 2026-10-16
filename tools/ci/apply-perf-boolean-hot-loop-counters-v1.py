#!/usr/bin/env python3
"""Optional diagnostic counters for the *current* Boolean range pass.

Mutates only the disposable CI checkout when explicitly invoked. These
counters do NOT measure CPU time or authorize an optimization; they count
candidate loops and useful work so runtime hotspots can be attributed.
"""
from pathlib import Path

path=Path("src/compiler/compiler.c")
text=path.read_text()
start="static bool minic_specialize_inline_boolean_range_calls(\n"
end="static bool minic_inline_integer_source_has_residual_reference(\n"
assert text.count(start)==1 and text.count(end)==1, "function anchors drifted"
a=text.index(start)
b=text.index(end)
func=text[a:b]


def rep(old,new,count=1):
    global func
    hits=func.count(old)
    if hits!=count:
        raise SystemExit(f"Boolean diagnostic anchor count={hits} != {count}: {old[:95]!r}")
    func=func.replace(old,new)

rep("""    size_t direct_clone_count = 0U;
    size_t transitive_clone_count = 0U;
""","""    size_t direct_clone_count = 0U;
    size_t transitive_clone_count = 0U;
    size_t diag_owner_eligible_callers = 0U;
    size_t diag_transitive_expression_visits = 0U;
    size_t diag_transitive_call_nodes = 0U;
    size_t diag_transitive_source_eligible = 0U;
    size_t diag_transitive_fact_positive = 0U;
    size_t diag_transitive_variant_probes = 0U;
    size_t diag_transitive_argument_probes = 0U;
    size_t diag_direct_variant_probes = 0U;
""")
rep("""        for (candidate_index = 0U; candidate_index < program->function_count;
             ++candidate_index) {
            if (minic_inline_boolean_range_variant_matches(""","""        for (candidate_index = 0U; candidate_index < program->function_count;
             ++candidate_index) {
            ++diag_direct_variant_probes;
            if (minic_inline_boolean_range_variant_matches(""")
rep("""            if (!caller_has_range) {
                continue;
            }
            for (nested_expression_index = 0U;
""","""            if (!caller_has_range) {
                continue;
            }
            ++diag_owner_eligible_callers;
            diag_transitive_expression_visits += program->expression_count;
            for (nested_expression_index = 0U;
""")
rep("""                if (nested->kind != MINIC_EXPRESSION_CALL ||
                    nested->value.call.function_id == MINIC_FUNCTION_INVALID) {
                    continue;
                }
                current_id = nested->value.call.function_id;""","""                if (nested->kind != MINIC_EXPRESSION_CALL ||
                    nested->value.call.function_id == MINIC_FUNCTION_INVALID) {
                    continue;
                }
                ++diag_transitive_call_nodes;
                current_id = nested->value.call.function_id;""")
rep("""                minic_inline_boolean_copy_existing_facts(
                    &current,
                    source.parameter_count,
                    integer_known,
                    integer_bits,
                    symbolic_known,
                    symbolic_expression,
                    boolean_range_known);
                for (parameter_index = 0U;""","""                ++diag_transitive_source_eligible;
                minic_inline_boolean_copy_existing_facts(
                    &current,
                    source.parameter_count,
                    integer_known,
                    integer_bits,
                    symbolic_known,
                    symbolic_expression,
                    boolean_range_known);
                for (parameter_index = 0U;""")
rep("""                    bool argument_used_caller_fact = false;
                    if (!minic_type_is_integer(source.parameter_types[parameter_index]) ||
                        !minic_inline_boolean_range_argument(""","""                    bool argument_used_caller_fact = false;
                    ++diag_transitive_argument_probes;
                    if (!minic_type_is_integer(source.parameter_types[parameter_index]) ||
                        !minic_inline_boolean_range_argument(""")
rep("""                if (!used_caller_fact || !adds_range) {
                    continue;
                }
                for (candidate_index = 0U;""","""                if (!used_caller_fact || !adds_range) {
                    continue;
                }
                ++diag_transitive_fact_positive;
                for (candidate_index = 0U;""")
rep("""                for (candidate_index = 0U;
                     candidate_index < program->function_count;
                     ++candidate_index) {
                    if (minic_inline_boolean_range_variant_matches(""","""                for (candidate_index = 0U;
                     candidate_index < program->function_count;
                     ++candidate_index) {
                    ++diag_transitive_variant_probes;
                    if (minic_inline_boolean_range_variant_matches(""")
rep("""    return true;
}

""","""    (void)fprintf(stderr,
                  "MINIC_BOOLEAN_HOT_LOOP_COUNTERS_V1 expressions=%zu functions=%zu "
                  "range_callers=%zu transitive_expression_visits=%zu "
                  "transitive_call_nodes=%zu transitive_source_eligible=%zu "
                  "transitive_argument_probes=%zu transitive_fact_positive=%zu "
                  "direct_variant_probes=%zu transitive_variant_probes=%zu "
                  "direct_clones=%zu transitive_clones=%zu\\n",
                  program->expression_count, program->function_count,
                  diag_owner_eligible_callers, diag_transitive_expression_visits,
                  diag_transitive_call_nodes, diag_transitive_source_eligible,
                  diag_transitive_argument_probes, diag_transitive_fact_positive,
                  diag_direct_variant_probes, diag_transitive_variant_probes,
                  direct_clone_count, transitive_clone_count);
    return true;
}

""")
modified=text[:a]+func+text[b:]
assert modified!=text and modified.count("MINIC_BOOLEAN_HOT_LOOP_COUNTERS_V1")==1
path.write_text(modified)
print("MINIC_BOOLEAN_DIAGNOSTIC_INSTRUMENTATION=APPLIED counters=11 production_source_committed=false")
