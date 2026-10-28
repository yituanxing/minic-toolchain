#!/usr/bin/env python3
"""Attest 21 frozen-GNU MiniC discovery receipts before grouping diagnostics.

Same-error text is only a diagnostic signature, not proof of a common Core
root cause.  GNU assembler/link/QEMU certification remains a separate gate.
"""
import argparse
import collections
import json
import re
from pathlib import Path

TARGETS = ("x86_64", "aarch64", "loongarch64")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    reports = sorted(args.root.glob("*/report.json"))
    if len(reports) != 21:
        raise SystemExit(f"THREE_LINUX_CENSUS=INCOMPLETE shards={len(reports)}/21")
    seen = set()
    compilers = set()
    groups = collections.defaultdict(lambda: collections.defaultdict(list))
    # These are genuine post-prune Core failure functions, not an inference
    # from the single first-error diagnostic of the translation unit.
    core_groups = collections.defaultdict(lambda: collections.defaultdict(list))
    # Enumerate real AArch64/LoongArch64 unsupported MACHINE instruction kinds.
    # A string saying "M1 unsupported" cannot justify treating all 400+
    # first barriers as a single interchangeable root cause.
    machine_kinds = collections.defaultdict(lambda: collections.defaultdict(list))
    totals = collections.defaultdict(lambda: dict(total=0, pass_count=0))
    source_state = {}
    for file in reports:
        receipt = json.loads(file.read_text())
        if receipt.get("verdict") != "MINIC_S_DISCOVERY_ONLY_AS_LINK_QEMU_NOT_TESTED":
            raise SystemExit("THREE_LINUX_CENSUS=FAIL not an execution-only source receipt")
        target, shard = receipt.get("target"), receipt.get("shard")
        if target not in TARGETS or type(shard) is not int or not 0 <= shard < 7:
            raise SystemExit("THREE_LINUX_CENSUS=FAIL wrong target/shard")
        if (target, shard) in seen:
            raise SystemExit("THREE_LINUX_CENSUS=FAIL duplicate target/shard")
        seen.add((target, shard))
        full = receipt.get("frozen_total")
        manifest = receipt.get("frozen_manifest_sha256")
        binary = receipt.get("compiler_sha256")
        identity = receipt.get("frozen_identity")
        profile = identity.get("profile") if isinstance(identity, dict) else None
        floor = 200 if profile == "common" else 1000
        if (type(full) is not int or full < floor or profile not in ("common", "broad")
                or not isinstance(identity, dict) or identity.get("target") != target
                or not isinstance(manifest, str) or not SHA256.fullmatch(manifest)
                or not isinstance(binary, str) or not SHA256.fullmatch(binary)):
            raise SystemExit("THREE_LINUX_CENSUS=FAIL invalid frozen/compiler identity")
        compilers.add(binary)
        profile = (full, manifest, json.dumps(identity, sort_keys=True))
        if target in source_state and source_state[target]["profile"] != profile:
            raise SystemExit("THREE_LINUX_CENSUS=FAIL mixed frozen GNU profiles")
        if target not in source_state:
            source_state[target] = dict(profile=profile, indices=set(), objects=set())
        state = source_state[target]
        rows = receipt.get("rows")
        if not isinstance(rows, list) or len(rows) != (full + 6 - shard) // 7:
            raise SystemExit(f"THREE_LINUX_CENSUS=FAIL incomplete {target} shard={shard}")
        for row in rows:
            index = row.get("index")
            obj = row.get("object")
            input_sha = row.get("input_sha256")
            if (type(index) is not int or index < 0 or index >= full
                    or index % 7 != shard
                    or not isinstance(obj, str) or not obj.endswith(".o")
                    or not isinstance(input_sha, str) or not SHA256.fullmatch(input_sha)):
                raise SystemExit("THREE_LINUX_CENSUS=FAIL corrupted owner/index/input SHA")
            if index in state["indices"] or obj in state["objects"]:
                raise SystemExit("THREE_LINUX_CENSUS=FAIL duplicate C owner")
            state["indices"].add(index)
            state["objects"].add(obj)
            frontiers = row.get("core_frontiers")
            if not isinstance(frontiers, list):
                raise SystemExit("THREE_LINUX_CENSUS=FAIL missing Core frontier census")
            core_indices = set()
            for item in frontiers:
                if (not isinstance(item, dict)
                        or item.get("kind") not in ("UNSUPPORTED", "ERROR")
                        or type(item.get("index")) is not int
                        or item["index"] < 0
                        or not isinstance(item.get("function"), str)
                        or not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", item["function"])
                        or item["index"] in core_indices):
                    raise SystemExit("THREE_LINUX_CENSUS=FAIL invalid Core frontier identity")
                core_indices.add(item["index"])
                core_groups[(item["kind"], item["function"])][target].append(obj)
            machine_frontiers = row.get("machine_frontiers", [])
            if not isinstance(machine_frontiers, list):
                raise SystemExit("THREE_LINUX_CENSUS=FAIL invalid machine trace list")
            for item in machine_frontiers:
                if (not isinstance(item,dict)
                    or type(item.get("kind")) is not int
                    or item["kind"] < 0
                    or not isinstance(item.get("function"),str)
                    or not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*",
                                        item["function"])
                    or type(item.get("block")) is not int
                    or type(item.get("instruction")) is not int):
                    raise SystemExit("THREE_LINUX_CENSUS=FAIL invalid machine reject")
                machine_kinds[(target,item["kind"])][item["function"]].append(obj)
            totals[target]["total"] += 1
            if row.get("status") == "MINIC_S_PASS":
                if frontiers:
                    raise SystemExit("THREE_LINUX_CENSUS=FAIL successful TU with failed Core functions")
                output_sha = row.get("asm_sha256")
                if not isinstance(output_sha, str) or not SHA256.fullmatch(output_sha):
                    raise SystemExit("THREE_LINUX_CENSUS=FAIL output without valid SHA")
                totals[target]["pass_count"] += 1
            else:
                signature = str(row.get("status", "UNKNOWN")) + " | " + str(row.get("diagnostic", ""))
                groups[signature][target].append(obj)
    if seen != {(arch, i) for arch in TARGETS for i in range(7)}:
        raise SystemExit("THREE_LINUX_CENSUS=FAIL missing shard")
    if len(compilers) != 1:
        raise SystemExit("THREE_LINUX_CENSUS=FAIL mixed MiniC binaries across 21 shards")
    for target in TARGETS:
        full = source_state[target]["profile"][0]
        if (source_state[target]["indices"] != set(range(full))
                or len(source_state[target]["objects"]) != full):
            raise SystemExit("THREE_LINUX_CENSUS=FAIL missing or duplicate owner")
    diagnostics = [
        dict(diagnostic_signature=signature,
             total=sum(len(v) for v in targets.values()),
             targets={k: sorted(v) for k, v in targets.items()})
        for signature, targets in groups.items()
    ]
    diagnostics.sort(key=lambda e: -e["total"])
    core_frontiers = [
        dict(kind=kind, function=function,
             total=sum(len(v) for v in owners.values()),
             targets={k: sorted(v) for k, v in owners.items()})
        for (kind, function), owners in core_groups.items()
    ]
    core_frontiers.sort(key=lambda e: (-e["total"], e["kind"], e["function"]))
    machine_kind_frontiers = [
        dict(target=arch,kind=kind,
             total=sum(len(v) for v in functions.values()),
             distinct_functions=len(functions),
             owners=sorted({o for names in functions.values() for o in names}))
        for (arch,kind),functions in machine_kinds.items()
    ]
    machine_kind_frontiers.sort(key=lambda x:(-x["total"],x["target"],x["kind"]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(
        schema="minic-linux-cross-target-diagnostic-census-v2",
        verdict="MINIC_S_DISCOVERY_ONLY_NOT_GNU_AS_LINK_OR_QEMU",
        compiler_sha256=next(iter(compilers)),
        targets=totals, diagnostic_groups=diagnostics,
        all_core_function_frontiers=core_frontiers,
        machine_kind_frontiers=machine_kind_frontiers), indent=2) + "\n")
    for arch, count in sorted(totals.items()):
        print(f"THREE_LINUX_MINIC_S target={arch} compiled={count['pass_count']}/{count['total']}")
    for item in diagnostics[:25]:
        targets = ",".join(sorted(item["targets"]))
        print(f"THREE_LINUX_DIAGNOSTIC total={item['total']} targets={targets} signature={item['diagnostic_signature'][:175]}")
    for arch in TARGETS:
        count = sum(len(item["targets"].get(arch, [])) for item in core_frontiers)
        functions = sum(arch in item["targets"] for item in core_frontiers)
        print(f"THREE_LINUX_ALL_CORE_FRONTIERS target={arch} failures={count} distinct_functions={functions}")
    for item in machine_kind_frontiers[:30]:
        print(f"THREE_LINUX_MACHINE_KIND target={item['target']} kind={item['kind']} "
              f"failures={item['total']} functions={item['distinct_functions']} "
              f"owners={len(item['owners'])}")
    print(f"THREE_LINUX_CENSUS=COMPLETE shards=21 diagnostic_signatures={len(diagnostics)} "
          f"all_core_function_groups={len(core_frontiers)} "
          "ROOT_CAUSE_REVIEW_REQUIRED=1 GNU_AS_link_QEMU=NOT_RUN")

if __name__ == "__main__":
    main()
