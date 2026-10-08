#!/usr/bin/env python3
"""Read-only (runtime-only) measurement instrumentation for Core IR verification.

Development-audit overlay, NOT a compiler semantics optimization. Emits
per-process aggregate counters under MINIC_CORE_VERIFY_AUDIT=1.
"""
from pathlib import Path

p = Path("src/core/core_ir.c")
s = p.read_text()
def replace(old, new):
    global s
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"CORE_VERIFY_AUDIT anchor matched {n} times: {old[:130]!r}")
    s = s.replace(old, new, 1)

replace("#include <inttypes.h>\n", "#include <inttypes.h>\n#include <stdio.h>\n")
counter = r'''
/* PERF_AUDIT_ONLY: no verifier semantic changes; aggregate at process exit. */
typedef struct MinicCoreVerifyAuditCounters {
    uint64_t calls;
    uint64_t blocks;
    uint64_t values;
    uint64_t cleared_bytes;
    uint64_t max_blocks;
    uint64_t max_values;
    uint64_t max_bv_product;
} MinicCoreVerifyAuditCounters;

static MinicCoreVerifyAuditCounters minic_core_verify_audit;

static void minic_core_verify_audit_dump(void) {
    (void)fprintf(stderr,
        "CORE_VERIFY_AUDIT calls=%" PRIu64 " blocks=%" PRIu64
        " sum_values=%" PRIu64 " memset_bytes=%" PRIu64
        " max_blocks=%" PRIu64 " max_values=%" PRIu64
        " max_bv=%" PRIu64 "\n",
        minic_core_verify_audit.calls,
        minic_core_verify_audit.blocks,
        minic_core_verify_audit.values,
        minic_core_verify_audit.cleared_bytes,
        minic_core_verify_audit.max_blocks,
        minic_core_verify_audit.max_values,
        minic_core_verify_audit.max_bv_product);
}

static bool minic_core_verify_audit_enabled(void) {
    static bool initialized = false;
    static bool enabled = false;
    if (!initialized) {
        const char *flag = getenv("MINIC_CORE_VERIFY_AUDIT");
        enabled = flag != NULL && flag[0] == '1';
        initialized = true;
        if (enabled) {
            (void)atexit(minic_core_verify_audit_dump);
        }
    }
    return enabled;
}

'''
replace("static bool verify_block(const MinicCoreFunction *function,\n",counter+"static bool verify_block(const MinicCoreFunction *function,\n")
replace(
"""    if (function->value_count != 0U) {
        (void)memset(available_values, 0, function->value_count * sizeof(*available_values));
    }
    for (index = 0U; index < block->instruction_count; ++index) {""",
"""    if (function->value_count != 0U) {
        if (minic_core_verify_audit_enabled()) {
            minic_core_verify_audit.cleared_bytes +=
                (uint64_t)function->value_count * sizeof(*available_values);
        }
        (void)memset(available_values, 0, function->value_count * sizeof(*available_values));
    }
    for (index = 0U; index < block->instruction_count; ++index) {""")
replace(
"""bool minic_core_function_verify(const MinicCoreFunction *function) {
    bool *available_values;""",
"""bool minic_core_function_verify(const MinicCoreFunction *function) {
    bool *available_values;""")
replace(
"""    bool valid;

    if (function == NULL || function->name == NULL""",
"""    bool valid;

    if (minic_core_verify_audit_enabled()) {
        minic_core_verify_audit.calls++;
        if (function != NULL) {
            uint64_t blocks = (uint64_t)function->block_count;
            uint64_t values = (uint64_t)function->value_count;
            minic_core_verify_audit.blocks += blocks;
            minic_core_verify_audit.values += values;
            if (blocks > minic_core_verify_audit.max_blocks) {
                minic_core_verify_audit.max_blocks = blocks;
            }
            if (values > minic_core_verify_audit.max_values) {
                minic_core_verify_audit.max_values = values;
            }
            if (blocks != 0U && values <= UINT64_MAX / blocks &&
                blocks * values > minic_core_verify_audit.max_bv_product) {
                minic_core_verify_audit.max_bv_product = blocks * values;
            }
        }
    }
    if (function == NULL || function->name == NULL""")
p.write_text(s)
print("MINIC_CORE_VERIFY_AUDIT_COUNTERS=APPLIED")
