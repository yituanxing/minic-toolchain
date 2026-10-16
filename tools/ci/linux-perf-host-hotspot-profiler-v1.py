#!/usr/bin/env python3
"""Profile the host MiniC compiler on a frozen Linux .i (NOT target QEMU runtime).

Prefer CPU-sampled perf if permitted; otherwise Callgrind records executed
instructions, call counts and source-line attribution. This is diagnostic
only and MUST NOT be interpreted as normal compile wall-time.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import time
from pathlib import Path


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as fd:
        for data in iter(lambda: fd.read(1 << 20), b""):
            h.update(data)
    return h.hexdigest()


def run(command, timeout, logfile):
    started = time.monotonic()
    try:
        with logfile.open("wb") as output:
            proc = subprocess.run(command, stdout=output,
                                  stderr=subprocess.STDOUT, timeout=timeout,
                                  check=False)
        return proc.returncode == 0, time.monotonic() - started
    except subprocess.TimeoutExpired:
        with logfile.open("ab") as output:
            output.write(b"\nPROFILER_TIMEOUT\n")
        return False, time.monotonic() - started


# Test-only source instrumentation. This is run inside the disposable CI
# checkout after the real production compiler has already been built.
def instrument_parser_probes():
    import re
    root = Path("src/frontend")
    header = root / "parser_internal.h"
    core = root / "parser_core.c"
    h = header.read_text()
    c = core.read_text()
    if h.count("} MinicParser;\n") != 1 or c.count("static bool minic_parser_grow_array(") != 1:
        raise RuntimeError("Parser source anchors changed")
    h = h.replace("} MinicParser;\n", "} MinicParser;\n\n"
                  "/* Profiling checkout only; never committed into production C. */\n"
                  "void minic_parser_copy_census_event(const char *file, unsigned line, size_t bytes);\n", 1)
    helper = r"""
/* Diagnostic-only: count nominal C struct copy volume, not DRAM traffic. */
typedef struct MinicParserCopySite {
    const char *file;
    unsigned line;
    size_t bytes;
    unsigned long long calls;
} MinicParserCopySite;
static MinicParserCopySite minic_copy_sites[128];
static size_t minic_copy_site_count;
static bool minic_copy_registered;
static void minic_parser_copy_dump(void) {
    size_t index;
    unsigned long long calls = 0ULL, bytes = 0ULL;
    for (index = 0U; index < minic_copy_site_count; ++index) {
        const MinicParserCopySite *site = &minic_copy_sites[index];
        calls += site->calls;
        bytes += site->calls * (unsigned long long)site->bytes;
        (void)fprintf(stderr,
                      "MINIC_PARSER_COPY_SITE file=%s line=%u calls=%llu size=%zu nominal_bytes=%llu\n",
                      site->file, site->line, site->calls, site->bytes,
                      site->calls * (unsigned long long)site->bytes);
    }
    (void)fprintf(stderr,
                  "MINIC_PARSER_COPY_TOTAL parser_bytes=%zu sites=%zu calls=%llu nominal_bytes=%llu\n",
                  sizeof(MinicParser), minic_copy_site_count, calls, bytes);
}
void minic_parser_copy_census_event(const char *file, unsigned line, size_t bytes) {
    size_t index;
    if (!minic_copy_registered) {
        if (atexit(minic_parser_copy_dump) != 0) abort();
        minic_copy_registered = true;
    }
    for (index = 0U; index < minic_copy_site_count; ++index) {
        MinicParserCopySite *site = &minic_copy_sites[index];
        if (site->line == line && strcmp(site->file, file) == 0) {
            if (site->bytes != bytes) abort();
            site->calls += 1ULL;
            return;
        }
    }
    if (minic_copy_site_count >= sizeof(minic_copy_sites) / sizeof(minic_copy_sites[0])) abort();
    minic_copy_sites[minic_copy_site_count].file = file;
    minic_copy_sites[minic_copy_site_count].line = line;
    minic_copy_sites[minic_copy_site_count].bytes = bytes;
    minic_copy_sites[minic_copy_site_count].calls = 1ULL;
    minic_copy_site_count += 1U;
}

"""
    c = c.replace("static bool minic_parser_grow_array(", helper +
                  "static bool minic_parser_grow_array(", 1)
    regex = re.compile(
        r"(?m)^([ \t]*)((?:MinicParser[ \t]+)?[A-Za-z_][A-Za-z_0-9]*[ \t]*=[ \t]*)\*parser;([ \t]*)$"
    )
    updates = []
    for path in sorted(root.glob("parser_*.c")):
        if path == core: continue
        changed, n = regex.subn(
            lambda m: m[1] + m[2] +
            "(minic_parser_copy_census_event(__FILE__, __LINE__, sizeof(*parser)), *parser);" +
            m[3],
            path.read_text(),
        )
        if n: updates.append((path, changed, n))
    count = sum(n for _, _, n in updates)
    if count < 30 or count > 110: raise RuntimeError("Unexpected copy sites: " + str(count))
    header.write_text(h)
    core.write_text(c)
    for path, changed, _ in updates: path.write_text(changed)
    print(f"MINIC_PARSER_COPY_INSTRUMENT=PASS sites={count} files={len(updates)} production_unmodified=true",
          flush=True)


def parse_parser_copy_census(out):
    import re
    for name in ("callgrind-run.log", "perf-run.log"):
        log = out / name
        if log.is_file(): break
    else: return None
    raw = log.read_text(errors="replace")
    totals = re.findall(
        r"^MINIC_PARSER_COPY_TOTAL parser_bytes=(\d+) sites=(\d+) calls=(\d+) nominal_bytes=(\d+)$",
        raw, re.MULTILINE,
    )
    if not totals: return None
    if len(totals) != 1: raise RuntimeError("ambiguous copy total")
    parser_bytes, nsites, calls, nominal_bytes = map(int, totals[0])
    rows = re.findall(
        r"^MINIC_PARSER_COPY_SITE file=(\S+) line=(\d+) calls=(\d+) size=(\d+) nominal_bytes=(\d+)$",
        raw, re.MULTILINE,
    )
    items = [dict(file=file, line=int(line), calls=int(count), size=int(size),
                  nominal_bytes=int(nbytes))
             for file, line, count, size, nbytes in rows]
    if (len(items) != nsites or sum(x["calls"] for x in items) != calls or
            sum(x["nominal_bytes"] for x in items) != nominal_bytes or
            nominal_bytes != calls * parser_bytes or parser_bytes < 131072):
        raise RuntimeError("inconsistent parser copy census")
    items.sort(key=lambda x: (-x["nominal_bytes"], x["file"], x["line"]))
    return dict(schema="minic-parser-copy-census-v1", parser_bytes=parser_bytes,
                calls=calls, nominal_bytes=nominal_bytes,
                nominal_gib=round(nominal_bytes / (1024 ** 3), 3),
                note="Nominal C struct copies; compiler may elide some; not measured CPU/DRAM bytes",
                sites=items)


def main():
    ap = argparse.ArgumentParser()
    for key in ("input", "debug-compiler", "release-compiler", "out"):
        ap.add_argument("--" + key, type=Path, required=True)
    ap.add_argument("--timeout", type=int, default=900)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for path in (args.input, args.debug_compiler, args.release_compiler):
        if not path.is_file():
            raise RuntimeError("Missing profiler file: " + str(path))
    expected = args.out / "release.s"
    ok, reference_time = run([str(args.release_compiler), "-S", str(args.input),
                              "-o", str(expected)], args.timeout,
                             args.out / "release.log")
    if not ok or not expected.is_file():
        raise RuntimeError("Release reference compiler failed")
    method = None
    instrumented_seconds = None
    if shutil.which("perf"):
        permitted, _ = run(["perf", "record", "-q", "-o",
                            str(args.out / "capability.data"), "--", "true"],
                           15, args.out / "perf-probe.log")
        if permitted:
            target = args.out / "perf.s"
            ok, instrumented_seconds = run(
                ["perf", "record", "-q", "-F", "99", "--call-graph", "dwarf",
                 "-o", str(args.out / "perf.data"), "--",
                 str(args.debug_compiler), "-S", str(args.input), "-o", str(target)],
                args.timeout, args.out / "perf-run.log")
            if ok and target.is_file() and digest(target) == digest(expected):
                ok_report, _ = run(["perf", "report", "--stdio", "-n",
                                    "-i", str(args.out / "perf.data")],
                                   90, args.out / "perf-top.txt")
                if not ok_report:
                    raise RuntimeError("perf sampled but report failed")
                method = "perf-sampled-cpu"
            elif ok:
                raise RuntimeError("perf compiler assembly differs from release")
    if method is None:
        if not shutil.which("valgrind") or not shutil.which("callgrind_annotate"):
            raise RuntimeError("No usable perf sampling or Callgrind installed")
        target = args.out / "callgrind.s"
        ok, instrumented_seconds = run(
            ["valgrind", "--tool=callgrind",
             "--callgrind-out-file=" + str(args.out / "callgrind.out"),
             "--log-file=" + str(args.out / "valgrind.log"),
             str(args.debug_compiler), "-S", str(args.input), "-o", str(target)],
            args.timeout, args.out / "callgrind-run.log")
        if not ok or not target.is_file():
            raise RuntimeError("Callgrind timed out or failed on frozen .i")
        if digest(target) != digest(expected):
            raise RuntimeError("instrumented .s differs from release .s")
        for fname, opts in (
            ("callgrind-self.txt", ["--inclusive=no", "--show=Ir"]),
            ("callgrind-inclusive.txt",
             ["--inclusive=yes", "--tree=both", "--show=Ir"])
        ):
            ok_ann, _ = run(["callgrind_annotate", "--auto=yes", *opts,
                             str(args.out / "callgrind.out")],
                            90, args.out / fname)
            if not ok_ann:
                raise RuntimeError("Callgrind annotation failed: " + fname)
        method = "callgrind-instructions-calls"
    parser_census = parse_parser_copy_census(args.out)
    if parser_census is not None:
        (args.out / "parser-copy-census.json").write_text(
            json.dumps(parser_census, indent=2) + "\n")
        print("MINIC_PARSER_COPY=PASS calls=%d nominal_gib=%.3f sites=%d" %
              (parser_census["calls"], parser_census["nominal_gib"],
               len(parser_census["sites"])), flush=True)
    result = {
        "schema": "minic-host-profiler-v1",
        "scope": "minic-cc host compilation of frozen .i; NOT GNU -E or QEMU",
        "input_sha256": digest(args.input),
        "release_cc_sha256": digest(args.release_compiler),
        "debug_cc_sha256": digest(args.debug_compiler),
        "assembly_equal": True, "assembly_sha256": digest(expected),
        "method": method,
        "reference_seconds": round(reference_time, 3),
        "instrumented_seconds": round(instrumented_seconds, 3),
        "warning": "Instrumented time is NOT normal-speed compiler A/B data."
    }
    (args.out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print("MINIC_HOTSPOT_PROFILE=PASS method=" + method
          + " input_sha256=" + result["input_sha256"]
          + " assembly_equal=true", flush=True)


if __name__ == "__main__":
    import sys
    if sys.argv[1:] == ["--instrument-parser-probes"]:
        instrument_parser_probes()
    else:
        main()
