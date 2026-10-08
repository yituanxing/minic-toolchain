#!/usr/bin/env python3
"""P12 measurement-only overlay for CoreObject scalar stack-slot reuse.

Apply AFTER apply-riscv64-core-object-slot-reuse-v0.py and its CFG hotfix.
Does not change reuse eligibility, overlap semantics, or placement.
Counters are per compilation process and emitted at exit only with
MINIC_CORE_OBJECT_SLOT_AUDIT=1. Timing is aggregate process CPU clock.
"""
from pathlib import Path

p = Path("src/target/riscv64/core_codegen.c")
src = p.read_text()

def once(old, new):
    global src
    count = src.count(old)
    if count != 1:
        raise SystemExit(f"P12 expected unique anchor, found {count}: {old[:120]!r}")
    src = src.replace(old, new, 1)

once("#include <string.h>\n", "#include <string.h>\n#include <time.h>\n")
counters = r'''
/* CORE_OBJECT_SLOT_AUDIT_V1: optional low-overhead aggregate observation.
 * Counts are approximate operations, NOT wall-clock TU time.
 * CPU clock() times are accumulated only for completed layout analyses. */
typedef struct MinicCoreObjectReuseAudit {
    uint64_t layout_builds;
    uint64_t interval_object_calls;
    uint64_t object_interval_instruction_visits;
    uint64_t address_value_use_scan_visits;
    uint64_t slot_conflict_comparisons;
    uint64_t actual_interval_overlap_checks;
    uint64_t analysis_ns;
    uint64_t coloring_ns;
    uint64_t objects_total;
    uint64_t instructions_total;
    uint64_t objects_max;
    uint64_t instructions_max;
} MinicCoreObjectReuseAudit;

static MinicCoreObjectReuseAudit minic_core_object_reuse_audit;

static void minic_core_object_reuse_audit_dump(void) {
    const MinicCoreObjectReuseAudit *p = &minic_core_object_reuse_audit;
    (void)fprintf(stderr,
        "CORE_OBJECT_SLOT_AUDIT builds=%" PRIu64
        " objects=%" PRIu64 " instructions=%" PRIu64
        " objects_max=%" PRIu64 " instructions_max=%" PRIu64
        " interval_calls=%" PRIu64
        " interval_instruction_visits=%" PRIu64
        " address_value_use_scan_visits=%" PRIu64
        " slot_conflict_comparisons=%" PRIu64
        " actual_overlap_checks=%" PRIu64
        " analysis_ns=%" PRIu64 " coloring_ns=%" PRIu64 "\n",
        p->layout_builds, p->objects_total, p->instructions_total,
        p->objects_max, p->instructions_max, p->interval_object_calls,
        p->object_interval_instruction_visits,
        p->address_value_use_scan_visits,
        p->slot_conflict_comparisons,
        p->actual_interval_overlap_checks,
        p->analysis_ns, p->coloring_ns);
}
static bool minic_core_object_reuse_audit_enabled(void) {
    static bool initialized;
    static bool enabled;
    if (!initialized) {
        const char *flag = getenv("MINIC_CORE_OBJECT_SLOT_AUDIT");
        enabled = flag != NULL && flag[0] == '1';
        initialized = true;
        if (enabled) {
            (void)atexit(minic_core_object_reuse_audit_dump);
        }
    }
    return enabled;
}
static uint64_t minic_core_object_reuse_cpu_elapsed_ns(clock_t start, clock_t stop) {
    if (start == (clock_t)-1 || stop == (clock_t)-1 || stop < start) {
        return 0U;
    }
    return (uint64_t)((double)(stop - start) * (1000000000.0 / CLOCKS_PER_SEC));
}

'''
once("static bool core_scalar_object_interval(const MinicC0Program *program,\n",
    counters + "static bool core_scalar_object_interval(const MinicC0Program *program,\n")
# Only edit inside the inserted stack-slot helper; avoid similarly named
# loops in the rest of the target.
begin = src.find("static bool core_scalar_object_interval(const MinicC0Program *program,")
end = src.find("static bool core_scalar_object_intervals_overlap(", begin)
if begin < 0 or end < begin:
    raise SystemExit("P12 cannot delimit object interval function")
fragment = src[begin:end]
marker = """    interval->reusable = false;
    interval->block_index = SIZE_MAX;"""
if fragment.count(marker) != 1:
    raise SystemExit("P12 interval initialization anchor mismatch")
fragment = fragment.replace(marker, """    if (minic_core_object_reuse_audit_enabled()) {
        minic_core_object_reuse_audit.interval_object_calls++;
    }
"""+marker, 1)
marker = """            MinicCoreInstructionId instruction_id = block->instructions[position];"""
if fragment.count(marker) != 2:
    raise SystemExit(f"P12 instruction-loop anchor expected twice; got {fragment.count(marker)}")
fragment = fragment.replace(marker, marker+"""
            if (minic_core_object_reuse_audit_enabled()) {
                minic_core_object_reuse_audit.object_interval_instruction_visits++;
            }""")
marker = """                        MinicCoreInstructionId use_id = candidate_block->instructions[use_position];"""
if fragment.count(marker) != 1:
    raise SystemExit("P12 address-use scan anchor mismatch")
fragment = fragment.replace(marker, marker+"""
                        if (minic_core_object_reuse_audit_enabled()) {
                            minic_core_object_reuse_audit.address_value_use_scan_visits++;
                        }""", 1)
src=src[:begin]+fragment+src[end:]

begin=src.find("static bool core_scalar_object_reuse_layout(const MinicC0Program *program,")
end=src.find("static bool core_frame_initialize(",begin)
if begin < 0 or end < begin:
    raise SystemExit("P12 cannot isolate scalar reuse layout")
fragment=src[begin:end]
marker = """        core_scalar_object_reuse_cache_clear();
        core_scalar_object_reuse_cache.program = program;"""
if fragment.count(marker)!=1:
    raise SystemExit("P12 cache build anchor mismatch")
fragment=fragment.replace(marker, """        if (minic_core_object_reuse_audit_enabled()) {
            MinicCoreObjectReuseAudit *p = &minic_core_object_reuse_audit;
            p->layout_builds++;
            p->objects_total += (uint64_t)function->object_count;
            p->instructions_total += (uint64_t)function->instruction_count;
            if (p->objects_max < function->object_count) {
                p->objects_max = (uint64_t)function->object_count;
            }
            if (p->instructions_max < function->instruction_count) {
                p->instructions_max = (uint64_t)function->instruction_count;
            }
        }
"""+marker,1)
marker = """            for (object_index = 0U; object_index < function->object_count; ++object_index) {
                core_scalar_object_reuse_cache.slots[object_index] = SIZE_MAX;"""
if fragment.count(marker)!=1:
    raise SystemExit("P12 analysis start loop anchor mismatch")
fragment=fragment.replace(marker,"""            clock_t object_analysis_start = clock();
"""+marker,1)
marker = """            for (object_index = 0U; object_index < function->object_count; ++object_index) {
                size_t slot;"""
if fragment.count(marker)!=1:
    raise SystemExit("P12 coloring start loop anchor mismatch")
fragment=fragment.replace(marker,"""            if (minic_core_object_reuse_audit_enabled()) {
                minic_core_object_reuse_audit.analysis_ns +=
                    minic_core_object_reuse_cpu_elapsed_ns(object_analysis_start, clock());
            }
            clock_t object_coloring_start = clock();
"""+marker,1)
marker="""                    for (prior = 0U; prior < object_index; ++prior) {
                        if (core_scalar_object_reuse_cache.slots[prior] == slot &&"""
if fragment.count(marker)!=1:
    raise SystemExit("P12 prior conflict loop anchor mismatch")
fragment=fragment.replace(marker,"""                    for (prior = 0U; prior < object_index; ++prior) {
                        if (minic_core_object_reuse_audit_enabled()) {
                            minic_core_object_reuse_audit.slot_conflict_comparisons++;
                            if (core_scalar_object_reuse_cache.slots[prior] == slot) {
                                minic_core_object_reuse_audit.actual_interval_overlap_checks++;
                            }
                        }
                        if (core_scalar_object_reuse_cache.slots[prior] == slot &&""",1)
marker="""            }
        }

        core_scalar_object_reuse_cache.peak_slots = peak;"""
if fragment.count(marker)!=1:
    raise SystemExit("P12 coloring end anchor mismatch")
fragment=fragment.replace(marker,"""            }
            if (minic_core_object_reuse_audit_enabled()) {
                minic_core_object_reuse_audit.coloring_ns +=
                    minic_core_object_reuse_cpu_elapsed_ns(object_coloring_start, clock());
            }
        }

        core_scalar_object_reuse_cache.peak_slots = peak;""",1)
src=src[:begin]+fragment+src[end:]
p.write_text(src)
print("CORE_OBJECT_SLOT_AUDIT_V1=APPLIED")
