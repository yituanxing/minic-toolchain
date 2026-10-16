#!/usr/bin/env python3
"""Audit exact Boolean-domain query/build/fallback counts for one Linux shard.

Only the Kbuild wrapper captures metrics from successful real MiniC processes.
Repeated owners (if Kbuild retried them) are declared; the final invocation is
used for per-owner totals. This does not measure PP, AS, or runtime speed.
"""
import argparse
from collections import Counter
import json
from pathlib import Path, PurePosixPath
import re

RECORD = re.compile(
    r"^([^\t]+)\tMINIC_BOOLEAN_DOMAIN queries=(\d+) builds=(\d+) fallbacks=(\d+)$"
)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--owners", type=Path, required=True)
    p.add_argument("--metrics", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    owners = [x.strip() for x in args.owners.read_text().splitlines()
              if x.strip() and not x.lstrip().startswith("#")]
    if not owners or len(set(owners)) != len(owners):
        raise SystemExit("MINIC_BOOLEAN_COUNTS=FAIL bad_manifest")
    expected = set(owners)
    for owner in owners:
        pos = PurePosixPath(owner)
        if pos.is_absolute() or ".." in pos.parts or not owner.endswith(".o"):
            raise SystemExit("MINIC_BOOLEAN_COUNTS=FAIL bad_owner="+owner)
    measured = {}
    repeat = Counter()
    for lineno, line in enumerate(args.metrics.read_text().splitlines(), 1):
        m = RECORD.fullmatch(line)
        if m is None:
            raise SystemExit(f"MINIC_BOOLEAN_COUNTS=FAIL bad_line={lineno}")
        owner = m[1]
        if owner not in expected:
            raise SystemExit("MINIC_BOOLEAN_COUNTS=FAIL unknown_owner="+owner)
        queries, builds, fallbacks = map(int, m.groups()[1:])
        if fallbacks > queries or builds > queries:
            raise SystemExit("MINIC_BOOLEAN_COUNTS=FAIL invalid_counters="+owner)
        measured[owner] = dict(queries=queries, builds=builds, fallbacks=fallbacks)
        repeat[owner] += 1
    missing = sorted(expected - measured.keys())
    if missing:
        raise SystemExit("MINIC_BOOLEAN_COUNTS=FAIL missing="+",".join(missing[:12]))
    totals = {key:sum(row[key] for row in measured.values())
              for key in ("queries","builds","fallbacks")}
    report = {
        "schema":"minic-boolean-native-seven-shard-v1",
        "owners":len(owners),
        "invocations":sum(repeat.values()),
        "repeated_owner_count":sum(v>1 for v in repeat.values()),
        "totals":totals,
        "top_queries":[{"owner":o,**v} for o,v in sorted(
            measured.items(),key=lambda item:item[1]["queries"],reverse=True)[:20]],
        "top_builds":[{"owner":o,**v} for o,v in sorted(
            measured.items(),key=lambda item:item[1]["builds"],reverse=True)[:20]],
        "per_owner":measured,
        "scope":"real MiniC C-TUs, not total kernel/QEMU time",
    }
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
    print("MINIC_BOOLEAN_COUNTS=PASS owners=%d invocations=%d recompiled_owners=%d queries=%d builds=%d fallbacks=%d" %
          (len(owners),report["invocations"],report["repeated_owner_count"],
           totals["queries"],totals["builds"],totals["fallbacks"]),flush=True)
    for entry in report["top_queries"][:5]:
        print("MINIC_BOOLEAN_COUNTS_HOT owner=%s queries=%d builds=%d fallbacks=%d" %
              (entry["owner"],entry["queries"],entry["builds"],entry["fallbacks"]),
              flush=True)

if __name__=="__main__":
    main()
