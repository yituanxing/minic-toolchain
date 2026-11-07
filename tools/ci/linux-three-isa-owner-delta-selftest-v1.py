#!/usr/bin/env python3
"""Synthetic exact-source 21-shard acceptance/delta regression harness.

The test distinguishes 3 newly passing owners from one newly regressing
owner. A net count of +2 must never be reported as 3 capabilities solved.
"""

import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile


HERE = Path(__file__).resolve().parent
TOOL = HERE / "linux-three-isa-owner-delta-v1.py"
spec = importlib.util.spec_from_file_location("minic_linux_owner_delta", TOOL)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


def make_campaign(root, compiler, after=False):
    for target in module.TARGETS:
        for shard in range(7):
            was_pass = shard < 4
            now_pass = (shard == 4 or was_pass) if after else was_pass
            if after and target == "loongarch64" and shard == 2:
                now_pass = False
            entry = dict(
                index=shard, object=f"kernel/example_{target}_{shard}.o",
                input_sha256=digest(f"original GNU Linux {target} {shard}"),
                status=module.PASS if now_pass else "MINIC_S_COMPILE_FAIL",
                diagnostic="" if now_pass else "real source unsupported")
            report = dict(
                verdict=module.VERDICT, target=target, shard=shard,
                frozen_total=7,
                frozen_manifest_sha256=digest(f"freeze {target}"),
                compiler_sha256=digest(compiler),
                frozen_identity=dict(target=target, profile="common",
                                     linux_commit="d1cfde2d"),
                rows=[entry])
            folder = root / f"{target}-{shard}"
            folder.mkdir(parents=True, exist_ok=True)
            (folder / "report.json").write_text(json.dumps(report))


def expect_invalid(root, target, shard, update, message, cross_only=False):
    path = root / f"{target}-{shard}" / "report.json"
    original = path.read_text()
    receipt = json.loads(original)
    update(receipt)
    path.write_text(json.dumps(receipt))
    try:
        right_campaign = module.read_campaign(root)
        if cross_only:
            module.compare(left, right_campaign)
    except ValueError:
        pass
    else:
        raise AssertionError(message)
    path.write_text(original)


with tempfile.TemporaryDirectory() as temp:
    work = Path(temp)
    before = work / "before"
    after = work / "after"
    make_campaign(before, "previous compiler", after=False)
    make_campaign(after, "fixed compiler", after=True)
    left = module.read_campaign(before)
    right = module.read_campaign(after)
    difference = module.compare(left, right)
    assert difference["total_before"] == 12
    assert difference["total_after"] == 14
    assert difference["total_gained"] == 3
    assert difference["total_regressed"] == 1
    assert difference["verdict"] == "SOURCE_S_ONLY_NOT_GNU_AS_LINK_OR_BOOT"
    for target in module.TARGETS:
        item = difference["targets"][target]
        assert item["total"] == 7
        assert item["before"] == 4
        assert item["gained"] == [f"kernel/example_{target}_4.o"]
        if target == "loongarch64":
            assert item["after"] == 4
            assert item["net"] == 0
            assert len(item["regressed"]) == 1
            assert item["regressed"][0]["owner"] == \
                "kernel/example_loongarch64_2.o"
        else:
            assert item["after"] == 5 and item["net"] == 1
            assert not item["regressed"]

    command = [sys.executable, str(TOOL), "--before-root", str(before),
               "--after-root", str(after), "--output", str(work / "delta.json")]
    completed = subprocess.run(command, text=True, capture_output=True,
                               check=True)
    assert "gained=3 regressed=1" in completed.stdout
    assert json.loads((work / "delta.json").read_text()) == difference
    failed = subprocess.run(command + ["--fail-on-regression"],
                            text=True, capture_output=True)
    assert failed.returncode != 0 and "REGRESSION" in failed.stderr

    expect_invalid(after, "aarch64", 1,
                   lambda r: r["rows"][0].update(input_sha256=digest("wrong .i")),
                   "tampered original Linux .i must be rejected", cross_only=True)
    expect_invalid(after, "loongarch64", 0,
                   lambda r: r.update(frozen_manifest_sha256=digest("wrong Kconfig")),
                   "mixed frozen source manifest must be rejected")
    expect_invalid(after, "x86_64", 3,
                   lambda r: r["rows"][0].update(index=2),
                   "shard-owner index mismatch must be rejected")
    expect_invalid(after, "x86_64", 5,
                   lambda r: r.update(compiler_sha256=digest("mixed compiler")),
                   "mixed compiler binaries must be rejected")
    expect_invalid(after, "x86_64", 6,
                   lambda r: r.update(rows=[]),
                   "missing owner status must be rejected")

    missing = after / "aarch64-6" / "report.json"
    saved = missing.read_text()
    missing.unlink()
    try:
        module.read_campaign(after)
    except ValueError:
        pass
    else:
        raise AssertionError("20/21 shards must fail, never silently pass")
    missing.write_text(saved)

    # Both individual campaigns can be internally consistent yet diverge
    # in the actual kernel .i input. Comparing them must still fail closed.
    changed = after / "x86_64-2" / "report.json"
    state = json.loads(changed.read_text())
    state["rows"][0]["input_sha256"] = digest("another valid-looking .i")
    changed.write_text(json.dumps(state))
    try:
        module.compare(module.read_campaign(before), module.read_campaign(after))
    except ValueError as exc:
        assert "GNU .i" in str(exc)
    else:
        raise AssertionError("changed GNU C .i must invalidate cross-run delta")

print("MINIC_THREE_LINUX_OWNER_DELTA=PASS shards=21x2 original_SHA=locked "
      "gained=3 regressed=1 net=2 incomplete=reject drift=reject")
