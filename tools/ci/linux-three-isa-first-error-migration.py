#!/usr/bin/env python3
"""Audit EACH frozen Linux owner, not merely cumulative pass counts.

The official 3x7 discovery summary preserves original first diagnostics for
all 612 failed owners. Seven current shard reports per target contain EVERY
source owner and its current first diagnostic. Comparing these lets us tell
"reached the next blocker" from "nothing happened", without pretending a
changed error message proves correct assembly, linking or runtime behavior.

The optional strict gate fails if ANY original failing owner has an identical
first diagnostic, or ANY previously passing owner regresses. An engineer may
use the non-strict report while iterating, but cannot claim 612/612 movement
until this gate actually succeeds against all 21 current shards.
"""

import argparse
import collections
import json
import pathlib
import re
import sys
import zipfile

ARCHES = ("x86_64", "aarch64", "loongarch64")
PASSED = "MINIC_S_PASS"


def canonical_diagnostic(text):
    """Compare cause, not CI runner/tmp directory or .s output filename.

    The original seven-way runner normalized C source positions but retained
    absolute ASSEMBLY-output `.s:1:1` paths, so naive string comparison
    would falsely call 190+ unchanged machine rejects "moved" merely because
    we reran them under /tmp instead of GitHub's /home/runner workspace.
    Preserve the FIRST compiler error reason, not pre-error census traces.
    150 characters cover concrete function/ISA reject identities while
    avoiding historical 240-character truncation artifacts.
    """
    if text is None:
        return ""
    normalized = re.sub(r"[^ :\n]+\.(?:i|c|h|s):\d+(?::\d+)?:",
                        "<source>:", str(text))
    for line in normalized.splitlines():
        if "error:" in line.lower():
            return "error: " + line.split("error:", 1)[1].strip()[:150]
    return "text: " + normalized.strip()[:150]


def read_json(path):
    path = pathlib.Path(path)
    if path.suffix.lower() != ".zip":
        return json.loads(path.read_text())
    with zipfile.ZipFile(path) as archive:
        json_files = [name for name in archive.namelist()
                      if name.endswith(".json") and not name.endswith("/")]
        if len(json_files) != 1:
            raise ValueError("baseline ZIP must contain exactly one JSON report")
        return json.loads(archive.read(json_files[0]))


def baseline_failures(summary, target):
    failures = {}
    if target not in summary.get("targets", {}):
        raise ValueError("missing target from baseline: " + target)
    for group in summary.get("diagnostic_groups", []):
        signature = group.get("diagnostic_signature")
        if not isinstance(signature, str) or " | " not in signature:
            raise ValueError("invalid old first diagnostic signature")
        old_status, old_error = signature.split(" | ", 1)
        if old_status != "MINIC_FAIL":
            raise ValueError("old report includes non-failure diagnostic group")
        for owner in group.get("targets", {}).get(target, []):
            if owner in failures:
                raise ValueError("duplicate failed owner in baseline: " + target + "/" + owner)
            failures[owner] = old_error
    expected = summary["targets"][target]
    if len(failures) != expected["total"] - expected["pass_count"]:
        raise ValueError("baseline first error coverage incomplete: " + target)
    return failures


def load_shards(folder, target):
    folder = pathlib.Path(folder)
    rows = {}
    shards_seen = set()
    compiler_hashes = set()
    manifests = set()
    frozen_sizes = set()
    for path in sorted(folder.glob("shard-*/report.json")):
        report = json.loads(path.read_text())
        if report.get("target") != target or report.get("total_shards") != 7:
            raise ValueError("wrong target/shard count: " + str(path))
        shard_id = report.get("shard")
        if not isinstance(shard_id, int) or shard_id not in range(7) or shard_id in shards_seen:
            raise ValueError("duplicate or invalid shard: " + str(path))
        shards_seen.add(shard_id)
        compiler_hashes.add(report.get("compiler_sha256"))
        manifests.add(report.get("frozen_manifest_sha256"))
        frozen_sizes.add(report.get("frozen_total"))
        for row in report.get("rows", []):
            owner = row.get("object")
            if not isinstance(owner, str) or not owner.endswith(".o") or owner in rows:
                raise ValueError("missing/duplicate source owner in " + str(path))
            if row.get("status") not in (PASSED, "MINIC_FAIL", "MINIC_TIMEOUT"):
                raise ValueError("invalid owner status: " + owner)
            if not isinstance(row.get("input_sha256"), str):
                raise ValueError("missing immutable input hash: " + owner)
            rows[owner] = row
    if shards_seen != set(range(7)):
        raise ValueError("not all seven REAL shards completed for " + target)
    if len(compiler_hashes) != 1 or None in compiler_hashes:
        raise ValueError("seven shards did not use the same MiniC binary")
    if len(manifests) != 1 or None in manifests:
        raise ValueError("shards have differing GNU frozen identities")
    if len(frozen_sizes) != 1 or next(iter(frozen_sizes)) != len(rows):
        raise ValueError("missing owners relative to immutable GNU manifest")
    return rows, next(iter(compiler_hashes)), next(iter(manifests))


def classify(original_failures, current_rows, baseline_target):
    total = baseline_target["total"]
    if len(current_rows) != total:
        raise ValueError("owner count changed within the comparison")
    if any(owner not in current_rows for owner in original_failures):
        raise ValueError("baseline failed owner missing from current census")
    records = []
    counts = collections.Counter({state: 0 for state in (
        "still_passing", "regressed", "now_passing",
        "first_error_unchanged", "next_blocker_exposed")})
    for owner, row in sorted(current_rows.items()):
        old_failed = owner in original_failures
        new_pass = row["status"] == PASSED
        old_first = original_failures.get(owner)
        new_first = row.get("diagnostic") if not new_pass else None
        if not old_failed and new_pass:
            state = "still_passing"
        elif not old_failed:
            state = "regressed"
        elif new_pass:
            state = "now_passing"
        elif canonical_diagnostic(old_first) == canonical_diagnostic(new_first):
            state = "first_error_unchanged"
        else:
            state = "next_blocker_exposed"
        counts[state] += 1
        records.append({
            "owner": owner,
            "input_sha256": row["input_sha256"],
            "baseline_first_error": old_first,
            "current_first_error": new_first,
            "status": row["status"],
            "movement": state,
        })
    if counts["still_passing"] + counts["regressed"] != baseline_target["pass_count"]:
        raise ValueError("baseline pass identities are inconsistent")
    if counts["now_passing"] + counts["next_blocker_exposed"] + \
       counts["first_error_unchanged"] != total - baseline_target["pass_count"]:
        raise ValueError("original failed owners missing from migration report")
    counts["old_pass"] = baseline_target["pass_count"]
    counts["current_pass"] = counts["still_passing"] + counts["now_passing"]
    counts["old_fail"] = total - baseline_target["pass_count"]
    counts["original_failures_moved"] = counts["now_passing"] + counts["next_blocker_exposed"]
    return dict(counts), records


def compare(summary, shard_dirs):
    results = {}
    compiler_hash = None
    for target in ARCHES:
        old_failures = baseline_failures(summary, target)
        rows, digest, manifest = load_shards(shard_dirs[target], target)
        if compiler_hash is not None and compiler_hash != digest:
            raise ValueError("three architectures did not use one compiler binary")
        compiler_hash = digest
        counts, records = classify(old_failures, rows, summary["targets"][target])
        results[target] = dict(census=counts, frozen_manifest_sha256=manifest,
                               files=records)
    total = collections.Counter()
    for target in ARCHES:
        total.update(results[target]["census"])
    return dict(schema="minic-three-isa-first-error-migration-v1",
                baseline_verdict=summary.get("verdict"),
                compiler_sha256=compiler_hash,
                target_count=3, shard_count=21,
                results=results, total=dict(total),
                verdict=("ALL_OLD_FIRST_BLOCKERS_MOVED_NO_REGRESSION"
                         if total["first_error_unchanged"] == 0 and total["regressed"] == 0
                         else "NOT_ALL_BLOCKERS_MOVED"))


def self_test():
    baseline_target = {"total": 4, "pass_count": 1}
    original = {"one.o": "<source>: error: one",
                "two.o": "<source>: error: two",
                "three.o": "<source>: error: three"}
    def row(owner, status, error=None):
        return {"object": owner, "status": status,
                "diagnostic": error, "input_sha256": owner + "_hash"}
    current = {
        "one.o": row("one.o", PASSED),
        "two.o": row("two.o", "MINIC_FAIL", "<source>: error: second layer"),
        "three.o": row("three.o", "MINIC_FAIL", "<source>: error: three"),
        "original_pass.o": row("original_pass.o", PASSED),
    }
    counts, records = classify(original, current, baseline_target)
    assert len(records) == 4 and counts["old_fail"] == 3
    assert counts["now_passing"] == 1
    assert counts["next_blocker_exposed"] == 1
    assert counts["first_error_unchanged"] == 1
    assert counts["regressed"] == 0
    assert counts["current_pass"] == 2
    current["three.o"] = row("three.o", "MINIC_FAIL", "third layer")
    counts, _ = classify(original, current, baseline_target)
    assert counts["original_failures_moved"] == 3
    assert counts["first_error_unchanged"] == 0
    current["original_pass.o"] = row("original_pass.o", "MINIC_FAIL", "regression")
    counts, _ = classify(original, current, baseline_target)
    assert counts["regressed"] == 1 and counts["current_pass"] == 1
    try:
        classify(original, {"one.o": row("one.o", PASSED)}, baseline_target)
    except ValueError:
        pass
    else:
        raise AssertionError("missing owners must be rejected")
    print("FIRST_ERROR_MIGRATION_SELFTEST=PASS states=5 next_layer=1 regression=1 missing_owner=1")
    return 0


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    if argv == ["--self-test"]:
        return self_test()
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--baseline", type=pathlib.Path, required=True)
    for target in ARCHES:
        p.add_argument("--" + target.replace("_", "-"), type=pathlib.Path,
                       required=True, dest=target)
    p.add_argument("--output", type=pathlib.Path, required=True)
    p.add_argument("--require-every-blocker-moved", action="store_true")
    args = p.parse_args(argv)
    try:
        summary = read_json(args.baseline)
        report = compare(summary, {target: getattr(args, target) for target in ARCHES})
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        for target in ARCHES:
            counts = report["results"][target]["census"]
            print("FIRST_ERROR_MIGRATION target={} old_pass={} new_pass={} moved={} "
                  "unchanged={} regressed={}".format(
                      target, counts["old_pass"], counts["current_pass"],
                      counts["original_failures_moved"],
                      counts["first_error_unchanged"], counts["regressed"]))
        total = report["total"]
        print("FIRST_ERROR_MIGRATION_21 total={} old_pass={} new_pass={} "
              "moved={}/{} unchanged={} regressed={} verdict={}".format(
                  sum(report["results"][t]["census"]["old_pass"] +
                      report["results"][t]["census"]["old_fail"] for t in ARCHES),
                  total["old_pass"], total["current_pass"],
                  total["original_failures_moved"], total["old_fail"],
                  total["first_error_unchanged"], total["regressed"],
                  report["verdict"]))
        if total["regressed"] != 0:
            return 2
        if args.require_every_blocker_moved and total["first_error_unchanged"] != 0:
            return 3
        return 0
    except (ValueError, OSError, KeyError, TypeError, json.JSONDecodeError) as exc:
        print("FIRST_ERROR_MIGRATION=ERROR " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
