#!/usr/bin/env python3
"""Export only failing SHA-frozen GNU .i files from a seven-way MiniC scan.

The complete GNU corpus remains immutable.  This compact, exact-input receipt
lets a developer reproduce every next C-only frontier without downloading a
gigabyte-sized successful-input archive.  It does not claim AS/link/runtime
correctness.
"""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import sys
import tempfile


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def relative_path(raw, prefix=None, suffix=None):
    if not isinstance(raw, str) or not raw:
        raise ValueError("missing GNU corpus path")
    path = PurePosixPath(raw)
    if path.is_absolute() or ".." in path.parts or "." in path.parts:
        raise ValueError("unsafe GNU corpus path: " + raw)
    if prefix is not None and path.parts[0] != prefix:
        raise ValueError("GNU input is not within the frozen corpus: " + raw)
    if suffix is not None and path.suffix != suffix:
        raise ValueError("unexpected GNU corpus path extension: " + raw)
    return path


def export(frozen_dir, report_path, output_dir, max_bytes=450 * 1024 * 1024):
    manifest_path = frozen_dir / "manifest.json"
    frozen = json.loads(manifest_path.read_text())
    report = json.loads(report_path.read_text())
    if frozen.get("schema") != "three-isa-gnu-i-v1":
        raise ValueError("invalid frozen GNU corpus schema")
    if report.get("schema") != "minic-linux-three-isa-shard-v1":
        raise ValueError("invalid seven-way MiniC report schema")
    target = frozen["identity"]["target"]
    if (report.get("target") != target
            or report.get("frozen_identity") != frozen["identity"]
            or report.get("frozen_manifest_sha256") != digest(manifest_path)):
        raise ValueError("seven-way report does not match its GNU source corpus")
    if frozen.get("errors"):
        raise ValueError("cannot export an incomplete GNU source corpus")
    rows = {row["index"]: row for row in frozen["rows"]}
    if len(rows) != len(frozen["rows"]):
        raise ValueError("duplicate GNU corpus index")
    planned, copied, total_bytes = [], [], 0
    seen_indices, seen_destinations = set(), set()
    for result in report["rows"]:
        if result["index"] in seen_indices:
            raise ValueError("duplicate MiniC report index")
        seen_indices.add(result["index"])
        status = result["status"]
        if status == "MINIC_S_PASS":
            continue
        if status not in ("MINIC_FAIL", "MINIC_TIMEOUT"):
            raise ValueError("unexpected MiniC source verdict: " + status)
        row = rows.get(result["index"])
        if (row is None or row["object"] != result["object"]
                or row["sha256"] != result["input_sha256"]
                or row["shard"] != report["shard"]):
            raise ValueError("failed object does not match frozen GNU owner")
        source_rel = relative_path(row["input"], prefix="i", suffix=".i")
        object_rel = relative_path(row["object"], suffix=".o")
        source = frozen_dir / source_rel
        if (source.is_symlink() or not source.is_file()
                or not source.resolve().is_relative_to(frozen_dir.resolve())
                or any((frozen_dir / Path(*source_rel.parts[:n])).is_symlink()
                       for n in range(1, len(source_rel.parts)))):
            raise ValueError("missing or symlink GNU frozen source: " + str(source_rel))
        size = source.stat().st_size
        total_bytes += size
        if digest(source) != row["sha256"]:
            raise ValueError("GNU frozen source SHA mismatch: " + str(source_rel))
        target_rel = Path("i") / object_rel.with_suffix(".i")
        if target_rel in seen_destinations:
            raise ValueError("duplicate focused GNU destination: " + str(target_rel))
        seen_destinations.add(target_rel)
        planned.append((row, status, source, size, target_rel))
    # Preflight the complete shard before materializing any files: an oversized
    # optional archive leaves only the authoritative report.json, never a
    # misleading subset of sources with no valid receipt.
    if total_bytes > max_bytes:
        raise ValueError("focused GNU failure corpus exceeds archive byte budget")
    # Keep optional artifacts transactional: failed copying or SHA validation
    # must never leave a partial directory that upload-artifact could archive.
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    if output_dir.exists() or output_dir.is_symlink():
        raise ValueError("focused GNU output already exists: " + str(output_dir))
    with tempfile.TemporaryDirectory(prefix=".focused-gnu-staging-",
                                     dir=output_dir.parent) as stage_name:
        staging = Path(stage_name)
        for row, status, source, size, target_rel in planned:
            destination = staging / target_rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            if digest(destination) != row["sha256"]:
                raise ValueError("focused GNU copy SHA mismatch: " + str(target_rel))
            copied.append({"object": row["object"], "path": target_rel.as_posix(),
                           "sha256": row["sha256"], "bytes": size,
                           "minic_flags": row["minic_flags"], "status": status})
        receipt = {"schema": "minic-focused-gnu-failing-inputs-v1",
                   "verdict": "EXACT_GNU_PREPROCESSED_INPUTS_C_ONLY_NO_AS_LINK_RUNTIME",
                   "target": target, "shard": report["shard"],
                   "compiler_sha256": report["compiler_sha256"],
                   "frozen_manifest_sha256": report["frozen_manifest_sha256"],
                   "total_bytes": total_bytes, "files": copied}
        (staging / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        staging.rename(output_dir)
    print("MINIC_FOCUSED_GNU_I=PASS target={} shard={} failures={} bytes={}".format(
        target, report["shard"], len(copied), total_bytes))
    return receipt


def self_test():
    with tempfile.TemporaryDirectory() as root_string:
        root = Path(root_string)
        frozen = root / "frozen"
        (frozen / "i/drivers").mkdir(parents=True)
        failing = frozen / "i/drivers/fail.i"
        passing = frozen / "i/drivers/pass.i"
        failing.write_text("int fail(void) { return 1; }\n")
        passing.write_text("int pass(void) { return 0; }\n")
        identity = {"target": "aarch64", "config_profile": "broad"}
        rows = [{"index": i, "shard": 3, "object": "drivers/{}.o".format(name),
                 "input": "i/drivers/{}.i".format(name),
                 "sha256": digest(frozen / "i/drivers/{}.i".format(name)),
                 "minic_flags": ["-fshort-wchar"]}
                for i, name in enumerate(("fail", "pass"))]
        manifest = {"schema": "three-isa-gnu-i-v1", "identity": identity,
                    "errors": [], "rows": rows}
        manifest_path = frozen / "manifest.json"
        manifest_path.write_text(json.dumps(manifest))
        report = {"schema": "minic-linux-three-isa-shard-v1",
                  "target": "aarch64", "shard": 3, "compiler_sha256": "test",
                  "frozen_identity": identity,
                  "frozen_manifest_sha256": digest(manifest_path),
                  "rows": [{"index": 0, "object": rows[0]["object"],
                            "input_sha256": rows[0]["sha256"], "status": "MINIC_FAIL"},
                           {"index": 1, "object": rows[1]["object"],
                            "input_sha256": rows[1]["sha256"], "status": "MINIC_S_PASS"}]}
        report_path = root / "report.json"
        report_path.write_text(json.dumps(report))
        out = root / "focused"
        receipt = export(frozen, report_path, out)
        assert len(receipt["files"]) == 1 and receipt["total_bytes"] == failing.stat().st_size
        assert (out / "i/drivers/fail.i").read_bytes() == failing.read_bytes()
        assert not (out / "i/drivers/pass.i").exists()
        for action in ("sha", "path", "identity", "budget"):
            if action == "sha":
                failing.write_text("different GNU source\n")
            elif action == "path":
                failing.write_text("int fail(void) { return 1; }\n")
                manifest["rows"][0]["input"] = "../../escape.i"
                manifest_path.write_text(json.dumps(manifest))
                report["frozen_manifest_sha256"] = digest(manifest_path)
                report_path.write_text(json.dumps(report))
            elif action == "identity":
                manifest["rows"][0]["input"] = "i/drivers/fail.i"
                manifest_path.write_text(json.dumps(manifest))
                report["frozen_manifest_sha256"] = "wrong"
                report_path.write_text(json.dumps(report))
            try:
                export(frozen, report_path, root / ("invalid-" + action),
                       max_bytes=1 if action == "budget" else 1024 * 1024)
            except ValueError:
                pass
            else:
                raise AssertionError("untrusted GNU receipt was accepted: " + action)
            if action == "identity":
                report["frozen_manifest_sha256"] = digest(manifest_path)
                report_path.write_text(json.dumps(report))
    print("MINIC_FOCUSED_GNU_I_SELFTEST=PASS sha path identity budget")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--frozen", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--max-bytes", type=int, default=450 * 1024 * 1024)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
        elif args.frozen is None or args.report is None or args.output is None:
            parser.error("--frozen, --report and --output are required")
        else:
            export(args.frozen, args.report, args.output, args.max_bytes)
        return 0
    except (OSError, KeyError, ValueError, AssertionError) as exc:
        print("MINIC_FOCUSED_GNU_I=FAIL " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
