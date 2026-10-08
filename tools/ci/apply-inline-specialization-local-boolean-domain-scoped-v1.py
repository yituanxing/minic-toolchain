#!/usr/bin/env python3
"""Activate the existing Boolean Domain index with safe per-compile lifetime.

The original linear implementation is retained as an allocation-failure fallback.
Cache validity tracks AST arena sizes, not merely the Program pointer.  A new
compilation explicitly clears it both at entry and on all normal exits.
"""
from pathlib import Path

original_path = Path("src/compiler/compiler.c")
source = original_path.read_text()
anchor = "static bool minic_inline_local_boolean_domain(\n"
end_marker = "\n}\n\nstatic bool minic_inline_boolean_range_argument("
assert source.count(anchor) == 1, "expected exactly one original Boolean Domain helper"
start = source.index(anchor)
end = source.find(end_marker, start)
if end < 0:
    raise SystemExit("cannot isolate legacy local Boolean Domain helper")
legacy = source[start:end + 3].replace(
    anchor, "static bool minic_inline_local_boolean_domain_legacy(\n", 1
)
index_script = Path("tools/ci/apply-inline-specialization-local-boolean-domain-index-v0.py")
exec(compile(index_script.read_text(), str(index_script), "exec"), {"__name__": "__main__"})
text = original_path.read_text()

def replace_one(old, new):
    global text
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"Boolean Domain scoped index anchor count={n}: {old[:120]!r}")
    text = text.replace(old, new, 1)

replace_one(
    "typedef struct MinicInlineLocalBooleanDomainCache {",
    legacy + "\n\n" + "typedef struct MinicInlineLocalBooleanDomainCache {",
)
replace_one(
    """    size_t local_count;
} MinicInlineLocalBooleanDomainCache;

static MinicInlineLocalBooleanDomainCache minic_inline_local_boolean_domain_cache;
""",
    """    size_t local_count;
    size_t statement_count;
    size_t expression_count;
    size_t inline_asm_count;
} MinicInlineLocalBooleanDomainCache;

static MinicInlineLocalBooleanDomainCache minic_inline_local_boolean_domain_cache;
static size_t minic_perf_boolean_domain_queries;
static size_t minic_perf_boolean_domain_builds;
static size_t minic_perf_boolean_domain_fallbacks;

static void minic_inline_local_boolean_domain_cache_clear(void) {
    free(minic_inline_local_boolean_domain_cache.values);
    (void)memset(&minic_inline_local_boolean_domain_cache, 0,
                 sizeof(minic_inline_local_boolean_domain_cache));
    minic_perf_boolean_domain_queries = 0U;
    minic_perf_boolean_domain_builds = 0U;
    minic_perf_boolean_domain_fallbacks = 0U;
}
""",
)
replace_one(
    """    minic_inline_local_boolean_domain_cache.local_count = program->local_count;
    return true;""",
    """    minic_inline_local_boolean_domain_cache.local_count = program->local_count;
    minic_inline_local_boolean_domain_cache.statement_count = program->statement_count;
    minic_inline_local_boolean_domain_cache.expression_count = program->expression_count;
    minic_inline_local_boolean_domain_cache.inline_asm_count = program->inline_asm_count;
    minic_perf_boolean_domain_builds += 1U;
    return true;""",
)
replace_one(
    """    if (program == NULL || local_id >= program->local_count) {
        return false;
    }
    if (minic_inline_local_boolean_domain_cache.program != program ||
        minic_inline_local_boolean_domain_cache.local_count != program->local_count ||
        minic_inline_local_boolean_domain_cache.values == NULL) {
        if (!minic_inline_build_local_boolean_domain_cache(program)) {
            return false;
        }
    }
    return minic_inline_local_boolean_domain_cache.values[local_id];""",
    """    if (program == NULL || local_id >= program->local_count) {
        return false;
    }
    minic_perf_boolean_domain_queries += 1U;
    /* Appended specialized functions share the original FunctionBody/LocalIds;
     * the arena-length guards cover the data used by the boolean write scan. */
    if (minic_inline_local_boolean_domain_cache.program != program ||
        minic_inline_local_boolean_domain_cache.local_count != program->local_count ||
        minic_inline_local_boolean_domain_cache.statement_count != program->statement_count ||
        minic_inline_local_boolean_domain_cache.expression_count != program->expression_count ||
        minic_inline_local_boolean_domain_cache.inline_asm_count != program->inline_asm_count ||
        minic_inline_local_boolean_domain_cache.values == NULL) {
        if (!minic_inline_build_local_boolean_domain_cache(program)) {
            /* Never confuse cache failure with 'not a Boolean Domain'. */
            minic_perf_boolean_domain_fallbacks += 1U;
            return minic_inline_local_boolean_domain_legacy(program, local_id);
        }
    }
    return minic_inline_local_boolean_domain_cache.values[local_id];""",
)
replace_one(
    """    buffer.data = NULL;
    buffer.size = 0U;
    if (!minic_read_file(input_path, &buffer, diagnostic)) {""",
    """    /* A stack-reused MinicC0Program pointer does not identify a new TU. */
    minic_inline_local_boolean_domain_cache_clear();
    buffer.data = NULL;
    buffer.size = 0U;
    if (!minic_read_file(input_path, &buffer, diagnostic)) {""",
)
replace_one(
    """    minic_core_function_set_destroy(&core_set);
    minic_c0_program_destroy(&program);
    free(buffer.data);
    return success ? 0 : 1;""",
    """    minic_core_function_set_destroy(&core_set);
    minic_c0_program_destroy(&program);
    if (getenv("MINIC_BOOLEAN_DOMAIN_PERF") != NULL) {
        (void)fprintf(stderr,
                      "MINIC_BOOLEAN_DOMAIN queries=%zu builds=%zu fallbacks=%zu\\n",
                      minic_perf_boolean_domain_queries,
                      minic_perf_boolean_domain_builds,
                      minic_perf_boolean_domain_fallbacks);
    }
    minic_inline_local_boolean_domain_cache_clear();
    free(buffer.data);
    return success ? 0 : 1;""",
)
original_path.write_text(text)
print("MINIC_PERF_BOOLEAN_DOMAIN_SCOPED_V1=APPLIED")
