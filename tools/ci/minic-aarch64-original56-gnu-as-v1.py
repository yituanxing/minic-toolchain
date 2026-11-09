#!/usr/bin/env python3
"""Replay 56 immutable Linux 6.6.143 AArch64 owners through MiniC and GNU AS.

The seven original artifacts were produced by Actions #42345808682. Pinning
their full index.json bytes also independently pins every .i and .cmd SHA.
No Linux build, fresh GNU preprocessing, or GCC fallback occurs in this lane.
"""

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys


SOURCE_RUN = 42345808682
INDEX_SHA256 = {
    0: "eacf73acf033788b1b8e7e182ae63c40f20b06be0ed9f4a323ae43c39885f911",
    1: "b32a75192ed406426c0790701adc5fcb90b2e7bff33a167659a84309939e7827",
    2: "971774e4950d2e682119594578869e0eeaa76420853d273b772a23e9264cbb73",
    3: "b2525e2250793d9be7aff45b654326d51a79103ed1dbfb17a78acc681677688d",
    4: "c7b230752ab622594f106e0a025a965433825e3cd02b7df7ef6d1e26ebbd380b",
    5: "88f11e7dc86a4a1af3dcd34b981533d90d76c01019dcd4a32245c0db95d50e5e",
    6: "5eb3f34bca595ec5049aacb2572ef92244a62804ac435b4905e1ea2772a2644f",
}
ORIGINAL_FLAGS = (
    "-fshort-wchar", "-funsigned-char", "-fno-PIE", "-mabi=lp64", "-march=armv8.5-a"
)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def execute(args, base, log_prefix):
    try:
        result = subprocess.run(
            args, cwd=base, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=240, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        pathlib.Path(str(log_prefix) + ".error").write_text(str(exc) + "\n")
        return None
    pathlib.Path(str(log_prefix) + ".stdout").write_text(result.stdout)
    pathlib.Path(str(log_prefix) + ".stderr").write_text(result.stderr)
    return result


def load_inputs(root):
    expected_artifacts = {f"minic-linux-seven-failure-aarch64-{i}" for i in INDEX_SHA256}
    actual_artifacts = {child.name for child in root.iterdir() if child.is_dir()}
    if actual_artifacts != expected_artifacts:
        raise ValueError(f"expected precisely 7 AArch64 Linux source artifacts, found {sorted(actual_artifacts)}")

    owners = []
    seen = set()
    for shard, pinned_index in INDEX_SHA256.items():
        artifact = root / f"minic-linux-seven-failure-aarch64-{shard}" / "failed-owner-diagnostics"
        index = artifact / "index.json"
        if sha256(index) != pinned_index:
            raise ValueError(f"shard={shard} official index SHA mismatch")
        samples = json.loads(index.read_text())["samples"]
        if len(samples) != 8:
            raise ValueError(f"shard={shard} expected 8 original failed owners, got {len(samples)}")
        for sample in samples:
            owner = sample["owner"]
            if (owner in seen or not owner.endswith(".o") or
                    pathlib.PurePosixPath(owner).is_absolute() or ".." in pathlib.PurePosixPath(owner).parts):
                raise ValueError(f"duplicate or unsafe original owner name: {owner}")
            seen.add(owner)
            stem = owner[:-2]
            source = artifact / stem / "original.gnu.i"
            command = artifact / stem / "original.gnu.o.cmd"
            pin_i = sample["files"]["original.gnu.i"]["sha256"]
            pin_cmd = sample["files"]["original.gnu.o.cmd"]["sha256"]
            if sha256(source) != pin_i or sha256(command) != pin_cmd:
                raise ValueError(f"original source/command SHA mismatch shard={shard} owner={owner}")
            if any(flag not in command.read_text() for flag in ORIGINAL_FLAGS):
                raise ValueError(f"GNU Kbuild ABI/ISA flag mismatch shard={shard} owner={owner}")
            owners.append((shard, owner, source, pin_i, pin_cmd))
    if len(owners) != 56:
        raise ValueError(f"expected exactly 56 SHA-pinned original Linux sources, got {len(owners)}")
    return sorted(owners)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact-root", type=pathlib.Path, required=True)
    parser.add_argument("--minic", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = args.artifact_root.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    inputs = load_inputs(root)
    print(f"AARCH_ORIGINAL56_INPUT=PASS owners={len(inputs)}/56 index_sha=7/7 input_sha=56/56 cmd_sha=56/56 source_run={SOURCE_RUN}", flush=True)

    results = []
    for shard, owner, source, pin_i, pin_cmd in inputs:
        prefix = out / (f"shard{shard}__" + owner[:-2].replace("/", "__"))
        asm = pathlib.Path(str(prefix) + ".s")
        obj = pathlib.Path(str(prefix) + ".o")
        row = {"shard": shard, "owner": owner, "input_sha256": pin_i,
               "cmd_sha256": pin_cmd, "status": "NOT_RUN"}
        results.append(row)
        # MiniC's AArch64 ISA/data layout is target-owned: AArch64 -march and
        # -mabi belong to GNU AS here, while C ABI flags must reach MiniC.
        cc = execute([str(args.minic.resolve()), "-S", str(source), "--target=aarch64-linux",
                      "-fshort-wchar", "-funsigned-char", "-fno-pie", "-o", str(asm)],
                     out, pathlib.Path(str(prefix) + ".minic"))
        if cc is None or cc.returncode:
            row["status"] = "MINIC_S_FAIL"
        else:
            row["asm_sha256"] = sha256(asm)
            gas = execute(["aarch64-linux-gnu-as", "-march=armv8.5-a", "-o", str(obj), str(asm)],
                          out, pathlib.Path(str(prefix) + ".gnu-as"))
            if gas is None or gas.returncode:
                row["status"] = "GNU_AS_FAIL"
            else:
                elf = execute(["aarch64-linux-gnu-readelf", "-h", str(obj)],
                              out, pathlib.Path(str(prefix) + ".readelf"))
                if (elf is None or elf.returncode or "AArch64" not in elf.stdout or
                        "REL (Relocatable file)" not in elf.stdout):
                    row["status"] = "ELF_MACHINE_OR_TYPE_FAIL"
                else:
                    row["obj_sha256"] = sha256(obj)
                    row["status"] = "MINIC_S_GNU_AS_PASS"
        print(f"AARCH_ORIGINAL56_OWNER={row['status']} shard={shard} owner={owner} source_sha={pin_i}", flush=True)

    (out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    passing = sum(row["status"] == "MINIC_S_GNU_AS_PASS" for row in results)
    print(f"AARCH_ORIGINAL56_GNU_AS={'PASS' if passing == 56 else 'FAIL'} owners={passing}/56 source_run={SOURCE_RUN} gcc_fallback=NO Linux_Image_P1=NOT_RUN", flush=True)
    return 0 if passing == 56 else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"AARCH_ORIGINAL56_GNU_AS=FAIL preflight={exc}", file=sys.stderr)
        sys.exit(1)
