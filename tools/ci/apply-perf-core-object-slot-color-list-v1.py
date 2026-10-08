#!/usr/bin/env python3
"""P13 experimental: linked per-slot occupant chains for CoreObject coloring.

For each candidate slot, check only objects already assigned that slot, not
every earlier object. First-fit slot choice and overlap predicate are intact.
If either optional index allocation fails, use the *original* quadratic scan.
Apply after scalar slot reuse V0 + CFG hotfix, before measurement overlay.
"""
from pathlib import Path

path=Path("src/target/riscv64/core_codegen.c")
s=path.read_text()
def once(old,new):
    global s
    n=s.count(old)
    if n!=1:
        raise SystemExit(f"P13 expected 1 anchor got {n}: {old[:105]!r}")
    s=s.replace(old,new,1)

once("""    CoreScalarObjectInterval *intervals;
    size_t *slots;
    size_t peak_slots;
} CoreScalarObjectReuseCache;""",
"""    CoreScalarObjectInterval *intervals;
    size_t *slots;
    /* Optional first-fit accelerator. Failure falls back to legacy scan. */
    size_t *slot_heads;
    size_t *slot_previous;
    size_t peak_slots;
} CoreScalarObjectReuseCache;""")
once("""static void core_scalar_object_reuse_cache_clear(void) {
    free(core_scalar_object_reuse_cache.slots);
    free(core_scalar_object_reuse_cache.intervals);""",
"""static void core_scalar_object_reuse_cache_clear(void) {
    free(core_scalar_object_reuse_cache.slot_heads);
    free(core_scalar_object_reuse_cache.slot_previous);
    free(core_scalar_object_reuse_cache.slots);
    free(core_scalar_object_reuse_cache.intervals);""")
once("""    core_scalar_object_reuse_cache.slots = NULL;
    core_scalar_object_reuse_cache.peak_slots = 0U;""",
"""    core_scalar_object_reuse_cache.slots = NULL;
    core_scalar_object_reuse_cache.slot_heads = NULL;
    core_scalar_object_reuse_cache.slot_previous = NULL;
    core_scalar_object_reuse_cache.peak_slots = 0U;""")
start=s.find("static bool core_scalar_object_reuse_layout(const MinicC0Program *program,")
end=s.find("static bool core_frame_initialize(",start)
if start<0 or end<=start:
    raise SystemExit("P13 cannot isolate layout")
f=s[start:end]
before="""            for (object_index = 0U; object_index < function->object_count; ++object_index) {
                core_scalar_object_reuse_cache.slots[object_index] = SIZE_MAX;"""
after="""            /* Extra arrays are an optional speed path, never a correctness
             * requirement. Both are full-size and independently owned. */
            core_scalar_object_reuse_cache.slot_heads =
                (size_t *)malloc(function->object_count *
                                 sizeof(*core_scalar_object_reuse_cache.slot_heads));
            core_scalar_object_reuse_cache.slot_previous =
                (size_t *)malloc(function->object_count *
                                 sizeof(*core_scalar_object_reuse_cache.slot_previous));
            if (core_scalar_object_reuse_cache.slot_heads == NULL ||
                core_scalar_object_reuse_cache.slot_previous == NULL) {
                free(core_scalar_object_reuse_cache.slot_heads);
                free(core_scalar_object_reuse_cache.slot_previous);
                core_scalar_object_reuse_cache.slot_heads = NULL;
                core_scalar_object_reuse_cache.slot_previous = NULL;
            } else {
                for (object_index = 0U;
                     object_index < function->object_count; ++object_index) {
                    core_scalar_object_reuse_cache.slot_heads[object_index] = SIZE_MAX;
                    core_scalar_object_reuse_cache.slot_previous[object_index] = SIZE_MAX;
                }
            }

"""+before
if f.count(before)!=1:raise SystemExit("P13 allocation anchor not unique")
f=f.replace(before,after,1)
old="""                    for (prior = 0U; prior < object_index; ++prior) {
                        if (core_scalar_object_reuse_cache.slots[prior] == slot &&
                            core_scalar_object_intervals_overlap(
                                &core_scalar_object_reuse_cache.intervals[object_index],
                                &core_scalar_object_reuse_cache.intervals[prior])) {
                            occupied = true;
                            break;
                        }
                    }"""
new="""                    if (core_scalar_object_reuse_cache.slot_heads != NULL) {
                        if (slot >= function->object_count) {
                            core_scalar_object_reuse_cache_clear();
                            return false;
                        }
                        for (prior = core_scalar_object_reuse_cache.slot_heads[slot];
                             prior != SIZE_MAX;
                             prior = core_scalar_object_reuse_cache.slot_previous[prior]) {
                            if (core_scalar_object_intervals_overlap(
                                    &core_scalar_object_reuse_cache.intervals[object_index],
                                    &core_scalar_object_reuse_cache.intervals[prior])) {
                                occupied = true;
                                break;
                            }
                        }
                    } else {
                        /* Exact historical first-fit fallback on memory pressure. */
                        for (prior = 0U; prior < object_index; ++prior) {
                            if (core_scalar_object_reuse_cache.slots[prior] == slot &&
                                core_scalar_object_intervals_overlap(
                                    &core_scalar_object_reuse_cache.intervals[object_index],
                                    &core_scalar_object_reuse_cache.intervals[prior])) {
                                occupied = true;
                                break;
                            }
                        }
                    }"""
if f.count(old)!=1:raise SystemExit(f"P13 conflict loop anchor count={f.count(old)}")
f=f.replace(old,new,1)
old="""                        if (slot == SIZE_MAX) {
                            core_scalar_object_reuse_cache_clear();
                            return false;
                        }
                        if (slot + 1U > peak) {"""
new="""                        if (slot == SIZE_MAX) {
                            core_scalar_object_reuse_cache_clear();
                            return false;
                        }
                        if (core_scalar_object_reuse_cache.slot_heads != NULL) {
                            core_scalar_object_reuse_cache.slot_previous[object_index] =
                                core_scalar_object_reuse_cache.slot_heads[slot];
                            core_scalar_object_reuse_cache.slot_heads[slot] = object_index;
                        }
                        if (slot + 1U > peak) {"""
if f.count(old)!=1:raise SystemExit("P13 insertion anchor not unique")
f=f.replace(old,new,1)
s=s[:start]+f+s[end:]
path.write_text(s)
print("MINIC_CORE_OBJECT_SLOT_COLOR_LIST_V1=APPLIED")
