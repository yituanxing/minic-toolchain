#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
s = p.read_text()

if not s.startswith("#define _POSIX_C_SOURCE 200809L\n"):
    s = "#define _POSIX_C_SOURCE 200809L\n" + s

if '#include <time.h>' not in s:
    anchor = '#include <string.h>\n'
    if anchor not in s:
        raise SystemExit("string include anchor missing")
    s = s.replace(anchor, anchor + '#include <time.h>\n', 1)

old = '''    (void)fprintf(stderr,
                  "MINIC_BOOTSTRAP_TRACE stage=%s state=%s functions=%zu input=%s\\n",
                  stage != NULL ? stage : "?",
                  state != NULL ? state : "?",
                  function_count,
                  input_path != NULL ? input_path : "?");
    (void)fflush(stderr);'''
new = '''    {
        struct timespec ts;
        uint64_t mono_ns = 0U;
        struct timespec cpu_ts;
        uint64_t cpu_ns = 0U;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
            mono_ns = (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
        }
        if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_ts) == 0) {
            cpu_ns = (uint64_t)cpu_ts.tv_sec * UINT64_C(1000000000) + (uint64_t)cpu_ts.tv_nsec;
        }
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=%s state=%s functions=%zu mono_ns=%" PRIu64
                      " cpu_ns=%" PRIu64 " input=%s\\n",
                      stage != NULL ? stage : "?",
                      state != NULL ? state : "?",
                      function_count,
                      mono_ns,
                      cpu_ns,
                      input_path != NULL ? input_path : "?");
        (void)fflush(stderr);
    }'''
if old not in s:
    raise SystemExit("bootstrap trace anchor missing")
s = s.replace(old, new, 1)

if '#include <inttypes.h>' not in s:
    anchor = '#include <errno.h>\n'
    if anchor not in s:
        raise SystemExit("errno include anchor missing")
    s = s.replace(anchor, anchor + '#include <inttypes.h>\n', 1)

p.write_text(s)
print("MINIC_PHASE_PROFILER_V0=APPLIED")
