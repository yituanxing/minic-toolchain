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
INTEGER_RECORD = re.compile(
    r"^([^\t]+)\tMINIC_INTEGER_OWNER_FILTER "
    r"ready=([01]) candidates=(\d+) skipped_other_owner=(\d+) fallback=([01])$"
)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--owners", type=Path, required=True)
    p.add_argument("--metrics", type=Path, required=True)
    p.add_argument("--integer-metrics", type=Path)
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
    ancillary = {}
    ancillary_repeat = Counter()
    for lineno, line in enumerate(args.metrics.read_text().splitlines(), 1):
        m = RECORD.fullmatch(line)
        if m is None:
            raise SystemExit(f"MINIC_BOOLEAN_COUNTS=FAIL bad_line={lineno}")
        owner = m[1]
        pos = PurePosixPath(owner)
        if pos.is_absolute() or ".." in pos.parts or len(pos.parts) < 2 or not owner.endswith(".o"):
            raise SystemExit("MINIC_BOOLEAN_COUNTS=FAIL bad_measured_owner="+owner)
        queries, builds, fallbacks = map(int, m.groups()[1:])
        if fallbacks > queries or builds > queries:
            raise SystemExit("MINIC_BOOLEAN_COUNTS=FAIL invalid_counters="+owner)
        row = dict(queries=queries, builds=builds, fallbacks=fallbacks)
        if owner in expected:
            measured[owner] = row
            repeat[owner] += 1
        else:
            # Kbuild may regenerate auxiliary native C objects (for example
            # vDSO) while producing the explicitly requested shard. The
            # existing MiniC route audit authenticates those extra outputs;
            # never misattribute their counters to this shard's owner set.
            ancillary[owner] = row
            ancillary_repeat[owner] += 1
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
        "ancillary_owners":len(ancillary),
        "ancillary_invocations":sum(ancillary_repeat.values()),
        "ancillary_per_owner":ancillary,
        "totals":totals,
        "top_queries":[{"owner":o,**v} for o,v in sorted(
            measured.items(),key=lambda item:item[1]["queries"],reverse=True)[:20]],
        "top_builds":[{"owner":o,**v} for o,v in sorted(
            measured.items(),key=lambda item:item[1]["builds"],reverse=True)[:20]],
        "per_owner":measured,
        "scope":"real MiniC C-TUs, not total kernel/QEMU time",
    }
    if args.integer_metrics is not None:
        # Reuse identical shard owner manifest and provenance requirements.
        # A repeated Kbuild invocation replaces that owner's earlier count.
        integer_per_owner = {}
        integer_ancillary = {}
        integer_repeats = Counter()
        integer_ancillary_repeats = Counter()
        for lineno, line in enumerate(args.integer_metrics.read_text().splitlines(), 1):
            match = INTEGER_RECORD.fullmatch(line)
            if match is None:
                raise SystemExit("MINIC_INTEGER_OWNER_COUNTS=FAIL bad_line="+str(lineno))
            owner, ready, candidates, skipped, fallback = match.groups()
            pos = PurePosixPath(owner)
            if pos.is_absolute() or ".." in pos.parts or len(pos.parts)<2 or not owner.endswith(".o"):
                raise SystemExit("MINIC_INTEGER_OWNER_COUNTS=FAIL invalid_owner="+owner)
            ready, candidates, skipped, fallback = map(
                int,(ready,candidates,skipped,fallback))
            if skipped > candidates or ready + fallback != 1 or (not ready and skipped):
                raise SystemExit("MINIC_INTEGER_OWNER_COUNTS=FAIL invalid_counters="+owner)
            row=dict(ready=ready,candidates=candidates,skipped_other_owner=skipped,
                     fallback=fallback)
            if owner in expected:
                integer_per_owner[owner]=row
                integer_repeats[owner]+=1
            else:
                integer_ancillary[owner]=row
                integer_ancillary_repeats[owner]+=1
        missing=sorted(expected-set(integer_per_owner))
        if missing:
            raise SystemExit("MINIC_INTEGER_OWNER_COUNTS=FAIL missing="+",".join(missing[:12]))
        if sum(integer_repeats.values()) != report["invocations"]:
            raise SystemExit("MINIC_INTEGER_OWNER_COUNTS=FAIL invocation_mismatch")
        total={key:sum(row[key] for row in integer_per_owner.values())
               for key in ("ready","candidates","skipped_other_owner","fallback")}
        report["integer_owner"]={
            "schema":"minic-integer-source-owner-census-v1",
            "per_owner":integer_per_owner,
            "ancillary_per_owner":integer_ancillary,
            "ancillary_invocations":sum(integer_ancillary_repeats.values()),
            "repeated_owner_count":sum(n>1 for n in integer_repeats.values()),
            "totals":total,
            "top_candidates":[{"owner":o,**v} for o,v in sorted(
                integer_per_owner.items(),key=lambda kv:kv[1]["candidates"],
                reverse=True)[:20]],
            "top_skipped":[{"owner":o,**v} for o,v in sorted(
                integer_per_owner.items(),key=lambda kv:kv[1]["skipped_other_owner"],
                reverse=True)[:20]],
            "scope":"Integer transitive CALL-loop iterations, not CPU time",
        }
        print("MINIC_INTEGER_OWNER_COUNTS=PASS owners=%d invocations=%d "
              "ready=%d fallback=%d candidates=%d skipped_other_owner=%d" %
              (len(integer_per_owner),sum(integer_repeats.values()),
               total["ready"],total["fallback"],total["candidates"],
               total["skipped_other_owner"]),flush=True)
        for entry in report["integer_owner"]["top_skipped"][:5]:
            print("MINIC_INTEGER_OWNER_HOT owner=%s candidates=%d skipped=%d" %
                  (entry["owner"],entry["candidates"],entry["skipped_other_owner"]),
                  flush=True)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
    print("MINIC_BOOLEAN_COUNTS=PASS owners=%d invocations=%d recompiled_owners=%d ancillary_owners=%d ancillary_invocations=%d queries=%d builds=%d fallbacks=%d" %
          (len(owners),report["invocations"],report["repeated_owner_count"],
           report["ancillary_owners"],report["ancillary_invocations"],
           totals["queries"],totals["builds"],totals["fallbacks"]),flush=True)
    for entry in report["top_queries"][:5]:
        print("MINIC_BOOLEAN_COUNTS_HOT owner=%s queries=%d builds=%d fallbacks=%d" %
              (entry["owner"],entry["queries"],entry["builds"],entry["fallbacks"]),
              flush=True)

if __name__=="__main__":
    main()
