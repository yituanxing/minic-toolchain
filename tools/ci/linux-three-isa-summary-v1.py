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
    # Source-lowering traces carry per-owner diagnostic evidence, not proof
    # that identical compiler text has the same architectural root cause.
    core_trace_groups = collections.defaultdict(lambda: collections.defaultdict(set))
    # Enumerate real AArch64/LoongArch64 unsupported MACHINE instruction kinds.
    # A string saying "M1 unsupported" cannot justify treating all 400+
    # first barriers as a single interchangeable root cause.
    machine_kinds = collections.defaultdict(lambda: collections.defaultdict(list))
    backend_stages = collections.defaultdict(lambda: collections.defaultdict(list))
    structured_reject_classes = collections.defaultdict(
        lambda: collections.defaultdict(set))
    precise_source_traces = []
    preflight_classes = collections.defaultdict(lambda: collections.defaultdict(list))
    asm_form_classes = collections.defaultdict(lambda: collections.defaultdict(list))
    core_asm_classes = collections.defaultdict(lambda: collections.defaultdict(set))
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
            traces = row.get("core_lower_traces", [])
            if not isinstance(traces, list):
                raise SystemExit("THREE_LINUX_CENSUS=FAIL invalid Core trace list")
            for trace in traces:
                if (not isinstance(trace, dict)
                        or not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*",
                                            str(trace.get("function", "")))
                        or not re.fullmatch(r"[a-z_]+", str(trace.get("stage", "")))
                        or not re.fullmatch(r"[a-z_-]+", str(trace.get("reason", "")))
                        or not re.fullmatch(r"(?:na|-?[0-9]+)", str(trace.get("kind", "")))
                        or not re.fullmatch(r"(?:na|-?[0-9]+)", str(trace.get("status", "")))):
                    raise SystemExit("THREE_LINUX_CENSUS=FAIL invalid Core trace")
                signature = (trace["stage"], trace["reason"],
                             str(trace["kind"]), str(trace["status"]))
                core_trace_groups[signature][target].add(obj)
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
            backend_frontiers = row.get("backend_frontiers",[])
            if not isinstance(backend_frontiers,list):
                raise SystemExit("THREE_LINUX_CENSUS=FAIL backend trace")
            for item in backend_frontiers:
                if (not isinstance(item,dict)
                   or type(item.get("kind")) is not int
                   or not isinstance(item.get("stage"),str)
                   or not isinstance(item.get("function"),str)
                   or not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*",
                                       item["function"])
                   or type(item.get("backend_line")) is not int):
                    raise SystemExit("THREE_LINUX_CENSUS=FAIL invalid backend owner")
                backend_stages[(target,item["stage"],item["kind"],
                                item["backend_line"])][item["function"]].append(obj)
            structured_rejects = row.get("structured_rejects", [])
            if not isinstance(structured_rejects, list):
                raise SystemExit("THREE_LINUX_CENSUS=FAIL structured ASM trace list")
            for item in structured_rejects:
                if (target != "aarch64" or not isinstance(item, dict)
                        or not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*",
                                            str(item.get("function", "")))
                        or not isinstance(item.get("preview"), str)
                        or len(item["preview"]) > 112
                        or any(ord(c) < 32 or ord(c) > 126
                               for c in item["preview"])
                        or any(type(item.get(field)) is not int
                               or item[field] < 0 for field in
                               ("template_length", "operand_count"))
                        or any(item.get(field) not in (0, 1) for field in
                               ("dynamic_frame", "volatile", "memory", "goto"))):
                    raise SystemExit("THREE_LINUX_CENSUS=FAIL invalid AArch64 ASM trace")
                signature = (item["preview"], item["template_length"],
                             item["operand_count"], item["dynamic_frame"],
                             item["volatile"], item["memory"], item["goto"])
                structured_reject_classes[signature][item["function"]].add(obj)
            source_traces = row.get("source_traces", [])
            if (not isinstance(source_traces, list)
                    or (source_traces and (target != "aarch64" or obj not in
                        ("sound/soc/soc-core.o", "drivers/tee/tee_core.o")))):
                raise SystemExit("THREE_LINUX_CENSUS=FAIL invalid source trace owner")
            for detail in source_traces:
                if (not isinstance(detail, str) or len(detail) > 430
                        or not re.fullmatch(
                            r"(?:AST_EXPR_DETAIL|CORE_EXPR_FAIL) [ -~]{1,400}",
                            detail)):
                    raise SystemExit("THREE_LINUX_CENSUS=FAIL invalid source trace")
                precise_source_traces.append(
                    dict(target=target, object=obj, detail=detail))
            for item in row.get("preflight_roots",[]):
                if (not isinstance(item,dict) or
                    not isinstance(item.get("reason"),str) or
                    not re.fullmatch("[a-z_]+",item["reason"]) or
                    not isinstance(item.get("function"),str) or
                    not re.fullmatch("[A-Za-z_][A-Za-z_0-9]*",
                                     item["function"])):
                    raise SystemExit("THREE_LINUX_CENSUS=FAIL invalid preflight root")
                preflight_classes[(target,item["reason"])][item["function"]].append(obj)
            for entry in row.get("asm_forms",[]):
                if (not isinstance(entry,dict) or
                    not isinstance(entry.get("hash"),str) or
                    not re.fullmatch(r"[0-9a-f]{16}",entry["hash"]) or
                    not isinstance(entry.get("preview"),str) or
                    not re.fullmatch(r"[A-Za-z_0-9]+",entry["preview"]) or
                    not isinstance(entry.get("function"),str)):
                    raise SystemExit("THREE_LINUX_CENSUS=FAIL invalid asm form")
                key=(target,entry["hash"],entry["preview"],
                     entry["template_length"])
                asm_form_classes[key][entry["function"]].append(obj)
            for entry in row.get("asm_frontiers", []):
                if (not isinstance(entry, dict)
                        or entry.get("reason") not in (
                            "target_required", "target_lower", "immediate_output",
                            "structured_roles", "fixed_register", "atomic_immediate",
                            "mixed_memory", "readwrite_output", "empty_barrier", "no_handler")
                        or not isinstance(entry.get("function"), str)
                        or not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", entry["function"])
                        or not isinstance(entry.get("preview"), str)
                        or not re.fullmatch(r"[A-Za-z_0-9]+", entry["preview"])
                        or not isinstance(entry.get("template_hash"), str)
                        or not re.fullmatch(r"[0-9a-f]{16}", entry["template_hash"])
                        or not isinstance(entry.get("signature_hash"), str)
                        or not re.fullmatch(r"[0-9a-f]{16}", entry["signature_hash"])
                        or any(type(entry.get(field)) is not int or entry[field] < 0
                               for field in ("template_length", "outputs", "inputs",
                                             "labels", "reg_clobbers", "clobbers"))
                        or entry.get("memory") not in (0, 1)
                        or entry.get("goto") not in (0, 1)
                        or entry.get("volatile") not in (0, 1)):
                    raise SystemExit("THREE_LINUX_CENSUS=FAIL invalid Core ASM frontier form")
                key = (target, entry["reason"], entry["signature_hash"],
                       entry["template_hash"], entry["preview"])
                core_asm_classes[key][entry["function"]].add(obj)
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
    core_trace_frontiers = [
        dict(stage=stage, reason=reason, kind=kind, status=status,
             affected_owners=sum(len(objects) for objects in targets.values()),
             targets={arch: sorted(objects) for arch, objects in targets.items()})
        for (stage, reason, kind, status), targets in core_trace_groups.items()
    ]
    core_trace_frontiers.sort(
        key=lambda e: (-e["affected_owners"], -len(e["targets"]),
                       e["stage"], e["reason"], e["kind"], e["status"]))
    machine_kind_frontiers = [
        dict(target=arch,kind=kind,
             total=sum(len(v) for v in functions.values()),
             distinct_functions=len(functions),
             owners=sorted({o for names in functions.values() for o in names}))
        for (arch,kind),functions in machine_kinds.items()
    ]
    machine_kind_frontiers.sort(key=lambda x:(-x["total"],x["target"],x["kind"]))
    backend_reject_frontiers = [
        dict(target=arch,stage=stage,kind=kind,backend_line=line,
             total=sum(len(v) for v in funcs.values()),
             distinct_functions=len(funcs),
             owners=sorted({o for vv in funcs.values() for o in vv}))
        for (arch,stage,kind,line),funcs in backend_stages.items()
    ]
    backend_reject_frontiers.sort(key=lambda x:(-x["total"],x["target"],x["stage"]))
    aarch64_structured_rejects = [
        dict(preview=preview,template_length=length,operand_count=operands,
             dynamic_frame=dynamic,volatile=volatile,memory=memory,goto=goto,
             functions=len(functions),
             owners=sorted({o for owners in functions.values() for o in owners}))
        for (preview,length,operands,dynamic,volatile,memory,goto),functions
        in structured_reject_classes.items()
    ]
    aarch64_structured_rejects.sort(
        key=lambda x:(-len(x["owners"]),-x["functions"],x["preview"]))
    preflight_root_frontiers = [
        dict(target=arch,reason=reason,
             instances=sum(len(v) for v in funcs.values()),
             functions=len(funcs),
             owners=sorted({o for arr in funcs.values() for o in arr}))
        for (arch,reason),funcs in preflight_classes.items()
    ]
    preflight_root_frontiers.sort(key=lambda x:(-x["instances"],x["target"]))
    asm_form_frontiers = [
        dict(target=arch,hash=h,preview=preview,template_length=length,
             instances=sum(len(v) for v in funcs.values()),
             functions=len(funcs),
             owners=sorted({o for xs in funcs.values() for o in xs}))
        for (arch,h,preview,length),funcs in asm_form_classes.items()
    ]
    asm_form_frontiers.sort(key=lambda x:(-x["instances"],x["target"],x["hash"]))
    core_asm_frontiers = [
        dict(target=target, reason=reason, signature_hash=signature,
             template_hash=template, preview=preview,
             functions=len(functions),
             owners=sorted({owner for names in functions.values() for owner in names}))
        for (target, reason, signature, template, preview), functions
        in core_asm_classes.items()
    ]
    core_asm_frontiers.sort(
        key=lambda x:(-len(x["owners"]), -x["functions"], x["target"], x["signature_hash"]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(
        schema="minic-linux-cross-target-diagnostic-census-v2",
        verdict="MINIC_S_DISCOVERY_ONLY_NOT_GNU_AS_LINK_OR_QEMU",
        compiler_sha256=next(iter(compilers)),
        targets=totals, diagnostic_groups=diagnostics,
        all_core_function_frontiers=core_frontiers,
         core_trace_frontiers=core_trace_frontiers,
        machine_kind_frontiers=machine_kind_frontiers,
        backend_reject_frontiers=backend_reject_frontiers,
        aarch64_structured_rejects=aarch64_structured_rejects,
        precise_source_traces=precise_source_traces,
        preflight_root_frontiers=preflight_root_frontiers,
        asm_form_frontiers=asm_form_frontiers,
        core_asm_frontiers=core_asm_frontiers), indent=2) + "\n")
    for arch, count in sorted(totals.items()):
        print(f"THREE_LINUX_MINIC_S target={arch} compiled={count['pass_count']}/{count['total']}")
    for item in diagnostics[:25]:
        targets = ",".join(sorted(item["targets"]))
        print(f"THREE_LINUX_DIAGNOSTIC total={item['total']} targets={targets} signature={item['diagnostic_signature'][:175]}")
    for arch in TARGETS:
        count = sum(len(item["targets"].get(arch, [])) for item in core_frontiers)
        functions = sum(arch in item["targets"] for item in core_frontiers)
        print(f"THREE_LINUX_ALL_CORE_FRONTIERS target={arch} failures={count} distinct_functions={functions}")
    for item in core_trace_frontiers[:40]:
        targets = ",".join(sorted(item["targets"]))
        print(f"THREE_LINUX_CORE_TRACE stage={item['stage']} reason={item['reason']} "
              f"kind={item['kind']} status={item['status']} "
              f"owners={item['affected_owners']} targets={targets}")
    for item in machine_kind_frontiers[:30]:
        print(f"THREE_LINUX_MACHINE_KIND target={item['target']} kind={item['kind']} "
              f"failures={item['total']} functions={item['distinct_functions']} "
              f"owners={len(item['owners'])}")
    for item in backend_reject_frontiers[:40]:
        print(f"THREE_LINUX_BACKEND_KIND target={item['target']} stage={item['stage']} "
              f"kind={item['kind']} source_line={item['backend_line']} "
              f"failures={item['total']} owners={len(item['owners'])}")
    for item in aarch64_structured_rejects[:40]:
        print(f"THREE_LINUX_AARCH64_STRUCTURED_REJECT "
              f"preview={item['preview'][:72]!r} length={item['template_length']} "
              f"operands={item['operand_count']} dynamic={item['dynamic_frame']} "
              f"volatile={item['volatile']} memory={item['memory']} "
              f"goto={item['goto']} functions={item['functions']} "
              f"owners={len(item['owners'])}")
    for item in precise_source_traces[:40]:
        print(f"THREE_LINUX_PRECISE_SOURCE_TRACE target={item['target']} "
              f"object={item['object']} detail={item['detail'][:300]}")
    for item in preflight_root_frontiers[:25]:
        print(f"THREE_LINUX_PREFLIGHT_ROOT target={item['target']} "
              f"reason={item['reason']} instances={item['instances']} "
              f"functions={item['functions']} owners={len(item['owners'])}")
    for entry in asm_form_frontiers[:40]:
        print(f"THREE_LINUX_ASM_FORM target={entry['target']} "
              f"hash={entry['hash']} prefix={entry['preview'][:48]} "
              f"length={entry['template_length']} "
              f"instances={entry['instances']} owners={len(entry['owners'])}")
    for entry in core_asm_frontiers[:40]:
        print(f"THREE_LINUX_ASM_FRONTIER target={entry['target']} "
              f"reason={entry['reason']} signature={entry['signature_hash']} "
              f"template={entry['template_hash']} preview={entry['preview'][:32]} "
              f"functions={entry['functions']} owners={len(entry['owners'])}")
    print(f"THREE_LINUX_CENSUS=COMPLETE shards=21 diagnostic_signatures={len(diagnostics)} "
          f"all_core_function_groups={len(core_frontiers)} "
          f"core_trace_groups={len(core_trace_frontiers)} "
          f"core_asm_groups={len(core_asm_frontiers)} "
          "ROOT_CAUSE_REVIEW_REQUIRED=1 GNU_AS_link_QEMU=NOT_RUN")

if __name__ == "__main__":
    main()
