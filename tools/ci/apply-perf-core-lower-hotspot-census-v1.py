#!/usr/bin/env python3
"""Inject diagnostic-only counts into a disposable Core Lowering source checkout.

No production C changes. Approximate potential O(Q*S + Q*E) traversal volume
is intentionally separate from measured wall time. Every input is an authentic
SHA-frozen Linux 6.6.143 Kbuild translation unit.
"""
from pathlib import Path
import re

file = Path("src/core/core_lower.c")
original = file.read_text()


def replace_exact(text: str, old: str, replacement: str, context: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"CORE_CENSUS_INSTRUMENT=FAIL {context} matches={count}")
    return text.replace(old, replacement, 1)


injection = r"""
/* Profile-only injected into the runner checkout, never tracked production C.
 * These are logical maximum scan lengths, not actual memory traffic or cycles.
 * Each MiniC process compiles exactly one Linux .i input in this experiment. */
typedef struct MinicCoreCensus {
    unsigned long long calls[5];
    unsigned long long candidate_statements[5];
    unsigned long long candidate_expressions[5];
} MinicCoreCensus;
static MinicCoreCensus minic_core_census;
static bool minic_core_census_registered;
static void minic_core_census_dump(void) {
    (void)fprintf(stderr,
                  "MINIC_CORE_LOWER_CENSUS map_calls=%llu map_slots=%llu "
                  "if_calls=%llu if_scan_statements=%llu if_scan_expressions=%llu "
                  "switch_calls=%llu switch_scan_statements=%llu switch_scan_expressions=%llu "
                  "unreachable_calls=%llu unreachable_scan_statements=%llu "
                  "loop_label_calls=%llu loop_label_scan_statements=%llu "
                  "loop_label_scan_expressions=%llu\n",
                  minic_core_census.calls[0],
                  minic_core_census.candidate_statements[0],
                  minic_core_census.calls[1],
                  minic_core_census.candidate_statements[1],
                  minic_core_census.candidate_expressions[1],
                  minic_core_census.calls[2],
                  minic_core_census.candidate_statements[2],
                  minic_core_census.candidate_expressions[2],
                  minic_core_census.calls[3],
                  minic_core_census.candidate_statements[3],
                  minic_core_census.calls[4],
                  minic_core_census.candidate_statements[4],
                  minic_core_census.candidate_expressions[4]);
}
static void minic_core_census_hit(unsigned int which, const MinicC0Program *program) {
    if (getenv("MINIC_CORE_LOWER_CENSUS") == NULL || program == NULL) {
        return;
    }
    if (!minic_core_census_registered) {
        if (atexit(minic_core_census_dump) != 0) abort();
        minic_core_census_registered = true;
    }
    if (which >= 5U) abort();
    minic_core_census.calls[which] += 1ULL;
    minic_core_census.candidate_statements[which] +=
        (unsigned long long)program->statement_count;
    minic_core_census.candidate_expressions[which] +=
        (unsigned long long)program->expression_count;
}

"""
updated = replace_exact(original, "#include <string.h>\n",
                        "#include <string.h>\n" + injection,
                        "core-census-global")

functions = (
    ("core_block_contains_reentry_label(", 1),
    ("core_switch_label_has_function_reentry(", 2),
    ("core_unreachable_statement_has_external_reentry(", 3),
    ("core_unreferenced_internal_loop_label(", 4),
)
for signature, which in functions:
    name = signature.rstrip("(")
    # Match a real static function definition, never a forward declaration or
    # incidental call from another helper (which could have its own 'program').
    pattern = re.compile(
        r"(?m)^static bool\s+" + re.escape(name) + r"\s*\([^;]*\)\s*\{"
    )
    matches = list(pattern.finditer(updated))
    if len(matches) != 1:
        raise SystemExit(
            f"CORE_CENSUS_INSTRUMENT=FAIL function={name} definitions={len(matches)}"
        )
    brace = matches[0].end() - 1
    end = updated.find("\n}", brace)
    if end < 0:
        raise SystemExit("CORE_CENSUS_INSTRUMENT=FAIL missing closure "+name)
    body = updated[brace:end + 2]
    before = "    program = context->body->program;"
    after = before + f"\n    minic_core_census_hit({which}U, program);"
    body = replace_exact(body, before, after, name)
    updated = updated[:brace] + body + updated[end + 2:]

map_anchor = (
    "    for (statement_index = 0U; statement_index < "
    "body->program->statement_count; ++statement_index) "
    "statement_blocks[statement_index] = MINIC_CORE_BLOCK_INVALID;"
)
updated = replace_exact(
    updated,
    map_anchor,
    map_anchor + "\n    minic_core_census_hit(0U, body->program);",
    "per-function-map-initialization",
)
file.write_text(updated)
print("MINIC_CORE_CENSUS_INSTRUMENT=PASS counters=5 production_source_committed=false", flush=True)
