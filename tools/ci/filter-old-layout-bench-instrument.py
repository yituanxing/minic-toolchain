#!/usr/bin/env python3
from pathlib import Path

p = Path("tools/ci/apply-riscv64-core-object-slot-reuse-v0.py")
s = p.read_text()

old = """    bool ok = false;

    if (program == NULL || function == NULL || peak_slots == NULL ||"""
new = """    bool ok = false;
    clock_t bench_start = 0;
    static size_t bench_call_count = 0U;
    size_t bench_call = 0U;

    if (function != NULL && function->object_count >= 1000U) {
        bench_call = ++bench_call_count;
        bench_start = clock();
    }

    if (program == NULL || function == NULL || peak_slots == NULL ||"""
if old not in s:
    raise SystemExit("bench start anchor missing")
s = s.replace(old, new, 1)

old = """done:
    free(slots);
    free(intervals);
    return ok;
}"""
new = """done:
    free(slots);
    free(intervals);
    if (bench_start != 0) {
        clock_t bench_end = clock();
        double bench_seconds =
            (double)(bench_end - bench_start) / (double)CLOCKS_PER_SEC;
        (void)fprintf(stderr,
                      "OLD_LAYOUT_BENCH call=%zu objects=%zu instructions=%zu "
                      "values=%zu cpu_ticks=%lld cpu_seconds=%.9f\\n",
                      bench_call,
                      function->object_count,
                      function->instruction_count,
                      function->value_count,
                      (long long)(bench_end - bench_start),
                      bench_seconds);
        (void)fflush(stderr);
    }
    return ok;
}"""
if old not in s:
    raise SystemExit("bench end anchor missing")
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
print("FILTER_OLD_LAYOUT_BENCH_INSTRUMENT=APPLIED")
