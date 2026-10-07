#!/usr/bin/env python3
from pathlib import Path

p = Path("tools/ci/apply-riscv64-core-object-slot-reuse-v0.py")
s = p.read_text()

old = """    size_t object_index;

    if (program == NULL || function == NULL || peak_slots == NULL ||"""
new = """    size_t object_index;
    clock_t model_start = 0;

    if (program == NULL || function == NULL || peak_slots == NULL ||"""
if old not in s:
    raise SystemExit("model declaration anchor missing")
s = s.replace(old, new, 1)

old = """        size_t peak = 0U;

        core_scalar_object_reuse_cache_clear();"""
new = """        size_t peak = 0U;

        if (function->object_count != 0U) {
            model_start = clock();
        }
        core_scalar_object_reuse_cache_clear();"""
if old not in s:
    raise SystemExit("model start anchor missing")
s = s.replace(old, new, 1)

old = """        core_scalar_object_reuse_cache.peak_slots = peak;
        if (getenv("MINIC_BOOTSTRAP_TRACE") != NULL) {"""
new = """        core_scalar_object_reuse_cache.peak_slots = peak;
        if (model_start != 0) {
            clock_t model_end = clock();
            double model_seconds =
                (double)(model_end - model_start) / (double)CLOCKS_PER_SEC;
            (void)fprintf(stderr,
                          "LAYOUT_MODEL objects=%zu instructions=%zu values=%zu "
                          "cpu_ticks=%lld cpu_seconds=%.9f\\n",
                          function->object_count,
                          function->instruction_count,
                          function->value_count,
                          (long long)(model_end - model_start),
                          model_seconds);
            (void)fflush(stderr);
        }
        if (getenv("MINIC_BOOTSTRAP_TRACE") != NULL) {"""
if old not in s:
    raise SystemExit("model end anchor missing")
s = s.replace(old, new, 1)

old = """p.write_text(text)
print("MINIC_RISCV64_CORE_OBJECT_SLOT_REUSE_V0=APPLIED")"""
new = """if '#include <time.h>' not in text:
    include_anchor = '#include <string.h>\\n'
    if include_anchor not in text:
        raise SystemExit("time include anchor missing")
    text = text.replace(include_anchor, include_anchor + '#include <time.h>\\n', 1)

p.write_text(text)
print("MINIC_RISCV64_CORE_OBJECT_SLOT_REUSE_V0=APPLIED")"""
if old not in s:
    raise SystemExit("write anchor missing")
s = s.replace(old, new, 1)

p.write_text(s)
print("FILTER_LAYOUT_MODEL_INSTRUMENT=APPLIED")
