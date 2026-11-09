#!/usr/bin/env python3
"""The 21-shard census must distinguish speculative traces from final status.

Core lowering logs are captured before reachability pruning. Keeping the
original trace provenance is essential, but a successful TU with a trace is
not evidence that the emitted translation unit contains a failed function.
"""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


SCRIPT = Path(__file__).with_name("linux-three-isa-summary-v1.py")


def sha(value):
    return hashlib.sha256(value.encode()).hexdigest()


def observation(index, target):
    trace = dict(function="helper", stage="statement", reason="lower-status",
                 kind="5", status="1")
    asm = dict(reason="structured_roles", function="helper", preview="fence",
               signature_hash="0123456789abcdef", template_hash="fedcba9876543210",
               template_length=5, outputs=0, inputs=0, labels=0,
               reg_clobbers=0, clobbers=0, memory=0, goto=0, volatile=1)
    status = "MINIC_S_COMPILE_FAIL" if index == 7 and target == "x86_64" else "MINIC_S_PASS"
    return dict(index=index, object=f"kernel/{target}_{index}.o",
                input_sha256=sha(f"GNU {target} {index}"), status=status,
                diagnostic="fixture compile failure" if status != "MINIC_S_PASS" else "",
                asm_sha256=sha(f"MiniC -S {target} {index}") if status == "MINIC_S_PASS" else None,
                core_frontiers=[] if status == "MINIC_S_PASS" else [
                    dict(kind="UNSUPPORTED", index=0, function="helper")],
                core_lower_traces=[trace] if index in (0, 7) and target == "x86_64" else [],
                asm_frontiers=[asm] if index in (0, 7) and target == "x86_64" else [])


with tempfile.TemporaryDirectory(prefix="minic-three-isa-phase-") as temp:
    root = Path(temp)
    for target in ("x86_64", "aarch64", "loongarch64"):
        for shard in range(7):
            rows = [observation(index, target)
                    for index in range(shard, 200, 7)]
            receipt = dict(
                verdict="MINIC_S_DISCOVERY_ONLY_AS_LINK_QEMU_NOT_TESTED",
                target=target, shard=shard,
                frozen_total=200,
                frozen_manifest_sha256=sha(f"original GNU {target}"),
                compiler_sha256=sha("one pinned MiniC binary"),
                frozen_identity=dict(profile="common", target=target,
                                     kernel_commit="pinned"), rows=rows)
            folder = root / f"{target}-{shard}"
            folder.mkdir(parents=True)
            (folder / "report.json").write_text(json.dumps(receipt))
    output = root / "summary.json"
    command = [sys.executable, str(SCRIPT), "--root", str(root),
               "--output", str(output)]
    completed = subprocess.run(command, text=True, capture_output=True,
                               check=True)
    summary = json.loads(output.read_text())
    assert summary["targets"]["x86_64"] == dict(total=200, pass_count=199)
    assert summary["targets"]["aarch64"] == dict(total=200, pass_count=200)
    assert len(summary["all_core_function_frontiers"]) == 1
    assert len(summary["core_trace_frontiers"]) == 2
    assert len(summary["core_asm_frontiers"]) == 2
    for name in ("core_trace_frontiers", "core_asm_frontiers"):
        items = summary[name]
        assert {item["observation_phase"] for item in items} == {
            "passing_tu_pre_prune_attempt", "failing_tu_pre_prune_attempt"}
        assert all(item.get("targets", {"x86_64": []}) or item.get("owners")
                   for item in items)
    assert "THREE_LINUX_ASM_FRONTIER phase=passing_tu_pre_prune_attempt" in completed.stdout
    assert "THREE_LINUX_ASM_FRONTIER phase=failing_tu_pre_prune_attempt" in completed.stdout
    print("MINIC_THREE_ISA_SUMMARY_PHASE=PASS passing_speculative=1 failing_live=1")
