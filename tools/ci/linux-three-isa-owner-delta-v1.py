#!/usr/bin/env python3
"""Compare two COMPLETE 21-shard frozen Linux source campaigns by C owner.

Reject mismatched original GNU inputs, broken shard manifests, duplicated
objects or missing results. A template-signature incidence is never counted
as a newly compiled owner. Neither -S result proves GNU AS, linking or boot.
"""

import argparse
import json
from pathlib import Path


TARGETS = ("x86_64", "aarch64", "loongarch64")
PASS = "MINIC_S_PASS"
VERDICT = "MINIC_S_DISCOVERY_ONLY_AS_LINK_QEMU_NOT_TESTED"


def read_campaign(root):
    """Return compiler, pinned GNU source identities and every owner verdict."""
    reports = sorted(root.glob("*/report.json"))
    if len(reports) != 21:
        raise ValueError(f"incomplete campaign: {len(reports)}/21 receipts")
    all_shards = set()
    compilers = set()
    profiles = {}
    rows = {target: {} for target in TARGETS}
    indices = {target: set() for target in TARGETS}

    for path in reports:
        receipt = json.loads(path.read_text())
        target = receipt.get("target")
        shard = receipt.get("shard")
        if (target not in TARGETS or type(shard) is not int or
                not 0 <= shard < 7 or receipt.get("verdict") != VERDICT or
                (target, shard) in all_shards):
            raise ValueError(f"invalid or duplicated receipt: {path}")
        all_shards.add((target, shard))
        count = receipt.get("frozen_total")
        manifest = receipt.get("frozen_manifest_sha256")
        identity = receipt.get("frozen_identity")
        compiler = receipt.get("compiler_sha256")
        if (type(count) is not int or count < 7 or
                not isinstance(identity, dict) or identity.get("target") != target or
                not isinstance(manifest, str) or len(manifest) != 64 or
                not isinstance(compiler, str) or len(compiler) != 64):
            raise ValueError(f"invalid frozen or compiler identity: {path}")
        profile = (count, manifest, json.dumps(identity, sort_keys=True))
        if target in profiles and profiles[target] != profile:
            raise ValueError(f"different frozen sources within {target}")
        profiles[target] = profile
        compilers.add(compiler)
        cohort = receipt.get("rows")
        if not isinstance(cohort, list) or len(cohort) != (count + 6 - shard) // 7:
            raise ValueError(f"incomplete owner cohort in {path}")
        for row in cohort:
            index = row.get("index")
            owner = row.get("object")
            original_sha = row.get("input_sha256")
            status = row.get("status")
            if (type(index) is not int or index < 0 or index >= count or
                    index % 7 != shard or index in indices[target] or
                    not isinstance(owner, str) or not owner.endswith(".o") or
                    not isinstance(original_sha, str) or len(original_sha) != 64 or
                    not isinstance(status, str) or not status):
                raise ValueError(f"bad or duplicated original C owner in {path}")
            indices[target].add(index)
            if owner in rows[target]:
                raise ValueError(f"duplicated owner name {target}:{owner}")
            rows[target][owner] = dict(
                index=index, input_sha256=original_sha, status=status,
                diagnostic=row.get("diagnostic", ""))

    if len(compilers) != 1 or all_shards != {
        (target, shard) for target in TARGETS for shard in range(7)
    }:
        raise ValueError("missing shard or mixed MiniC binary within one campaign")
    for target in TARGETS:
        if indices[target] != set(range(profiles[target][0])):
            raise ValueError(f"missing original GNU C owners in {target}")
    return dict(compiler=next(iter(compilers)), profiles=profiles, rows=rows)


def compare(before, after):
    outcome = {}
    for target in TARGETS:
        if before["profiles"][target] != after["profiles"][target]:
            raise ValueError(f"frozen manifest/Kconfig changed for {target}")
        original, revised = before["rows"][target], after["rows"][target]
        if original.keys() != revised.keys():
            raise ValueError(f"GNU C object inventory changed for {target}")
        gained, regressed, same_pass, still_rejected, changed_error = [], [], [], [], []
        for owner in sorted(original):
            old, new = original[owner], revised[owner]
            if (old["index"], old["input_sha256"]) != (
                    new["index"], new["input_sha256"]):
                raise ValueError(f"original GNU .i SHA or index drift: {target}:{owner}")
            was_pass = old["status"] == PASS
            now_pass = new["status"] == PASS
            if now_pass and not was_pass:
                gained.append(owner)
            elif was_pass and not now_pass:
                regressed.append(dict(owner=owner, diagnostic=new["diagnostic"]))
            elif was_pass:
                same_pass.append(owner)
            else:
                still_rejected.append(owner)
                if old["status"] != new["status"]:
                    changed_error.append(dict(owner=owner, old=old["status"],
                                              new=new["status"]))
        old_count = len(regressed) + len(same_pass)
        new_count = len(gained) + len(same_pass)
        outcome[target] = dict(
            total=len(original), before=old_count, after=new_count,
            net=new_count - old_count, gained=gained, regressed=regressed,
            still_rejected=len(still_rejected), changed_reject_status=changed_error)
    return dict(
        schema="minic-linux-three-isa-exact-owner-delta-v1",
        verdict="SOURCE_S_ONLY_NOT_GNU_AS_LINK_OR_BOOT",
        baseline_compiler_sha256=before["compiler"],
        current_compiler_sha256=after["compiler"],
        targets=outcome,
        total_before=sum(x["before"] for x in outcome.values()),
        total_after=sum(x["after"] for x in outcome.values()),
        total_gained=sum(len(x["gained"]) for x in outcome.values()),
        total_regressed=sum(len(x["regressed"]) for x in outcome.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before-root", type=Path, required=True)
    parser.add_argument("--after-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fail-on-regression", action="store_true")
    args = parser.parse_args()
    try:
        result = compare(read_campaign(args.before_root),
                         read_campaign(args.after_root))
    except (ValueError, KeyError, OSError, json.JSONDecodeError) as e:
        parser.error(f"THREE_LINUX_OWNER_DELTA=INVALID {e}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    for target in TARGETS:
        row = result["targets"][target]
        print(f"THREE_LINUX_OWNER_DELTA target={target} "
              f"{row['before']}/{row['total']} -> {row['after']}/{row['total']} "
              f"gained={len(row['gained'])} regressed={len(row['regressed'])} "
              f"net={row['net']}")
    print(f"THREE_LINUX_OWNER_DELTA total={result['total_before']}"
          f"->{result['total_after']} gained={result['total_gained']} "
          f"regressed={result['total_regressed']} "
          f"verdict={result['verdict']}")
    if args.fail_on_regression and result["total_regressed"]:
        raise SystemExit("THREE_LINUX_OWNER_DELTA=REGRESSION")


if __name__ == "__main__":
    main()
