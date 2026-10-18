#!/usr/bin/env python3
"""Independently certify archived seven-way Stage4 Linux 2064 + QEMU P1.

Uses immutable GitHub Actions artifacts already produced by:
- nine native selfhost tools: run 39058935623
- seven real Kbuild producer shards and linker/QEMU receiver: run 39063287653

NO Linux recompilation, no QEMU replay, no GNU executable in this witness.
Every .o is checked against its saved shard SHA and merged receiver SHA;
every canonical C owner must have a genuine Stage4 QEMU -S dispatch.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys

COMPILER_SHA = "50b3f7763bde2505e17bb545513cb00664286d4e762b3395aea3f29ce724ef31"
IMAGE_SHA = "6e0d275025f71c9daf9084f87946e693a9f5da3d71db712a2b6c4348e308d667"
RUN_ID = 39063287653


def digest(p):
    h = hashlib.sha256()
    with p.open("rb") as f:
        for data in iter(lambda: f.read(1 << 20), b""):
            h.update(data)
    return h.hexdigest()


def require(condition, problem):
    if not condition:
        raise ValueError(problem)


def manifest(path):
    result = {}
    for line in path.read_text().splitlines():
        m = re.fullmatch(r"([0-9a-f]{64})  minic/(.+\.o)", line)
        require(m is not None, "malformed SHA manifest: " + line[:120])
        name = m.group(2)
        require(name not in result and ".." not in Path(name).parts,
                "duplicate or unsafe object: " + name)
        result[name] = m.group(1)
    return result


def owners(path):
    lines = [x.strip() for x in path.read_text().splitlines()
             if x.strip() and not x.lstrip().startswith("#")]
    require(len(lines) == len(set(lines)), "duplicate owner in " + str(path))
    require(all(x.endswith(".o") and ".." not in Path(x).parts for x in lines),
            "unexpected owner path")
    return lines


def inspect_producer(p, index):
    names = owners(p / "full-objects.txt")
    expected = 294 if index == 6 else 295
    require(len(names) == expected, "shard %s owner count %s" % (index, len(names)))
    hashed = manifest(p / "minic-manifest.sha256")
    require(set(names) == set(hashed), "shard %s owner/SHA manifest mismatch" % index)
    status = (p / "cohort.log").read_text()
    for tag in ("COHORT_COMPILE=PASS objects=",
                "COHORT_ABI=PASS objects=",
                "COHORT_PRODUCE_ONLY=PASS objects="):
        require(tag + str(expected) in status,
                "missing producer %s evidence shard=%s" % (tag, index))
    pairs = []
    for row in (p / "stage4-invocations.tsv").read_text().splitlines():
        parts = row.split("\t")
        require(len(parts) == 2 and all(parts), "malformed Stage4 QEMU compiler trace")
        pairs.append(parts)
    outs = Counter(x[1].removeprefix("./") for x in pairs)
    for name in names:
        expect = name[:-2] + ".minic-stage2.s"  # frozen Kbuild suffix, not version
        require(any(out == expect or out.endswith("/" + expect) for out in outs),
                "missing REAL QEMU Stage4 compiler dispatch for " + name)
        obj = p / "minic" / name
        require(obj.is_file() and digest(obj) == hashed[name],
                "object SHA not equal to attested compiler output: " + name)
    print("STAGE4_LINUX2064_SHARD=PASS index=%s owners=%s guest_traces=%s" %
          (index, expected, len(pairs)), flush=True)
    return hashed


def run(receiver, producers, native, output):
    rv64 = native / "full-native/stage4/bin/minic-cc"
    report = json.loads((native / "full-native/stage4/summary.json").read_text())
    require(rv64.is_file() and digest(rv64) == COMPILER_SHA and
            report["round"] == "stage4" and report["passed"] == 77 and
            not report["failed"] and len(report["linked"]) == 9 and
            report["linked"]["minic-cc"] == COMPILER_SHA,
            "not the immutable 77-TU nine-tool Stage4 compiler")
    proof = receiver / "linux-runtime-gcc-cohort-v1"
    real_owners = owners(proof / "full-objects.txt")
    require(len(real_owners) == 2064, "full-objects.txt has wrong REAL owner count")
    assert_manifest = manifest(proof / "minic-manifest.sha256")
    require(len(assert_manifest) == 2064 and set(real_owners) == set(assert_manifest),
            "receiver union owner/manifest mismatch")
    by_shard = {}
    combined = {}
    for index in range(7):
        p = producers / ("shard-%d" % index)
        require(p.is_dir(), "missing producer artifact shard-%d" % index)
        hashes = inspect_producer(p, index)
        for owner, sha in hashes.items():
            require(owner not in combined, "overlap among seven producer shards: " + owner)
            combined[owner] = sha
        by_shard[str(index)] = len(hashes)
    require(combined == assert_manifest,
            "receiver SHA union differs from seven immutable producer objects")
    logs = (proof / "cohort.log").read_text()
    for needle in (
        "COHORT_SCOPE=FULL_C_UNIVERSE objects=2064",
        "COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS",
        "COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true",
    ):
        require(needle in logs, "QEMU receiver verdict missing: " + needle)
    image = proof / "trials/full_all/Image"
    require(image.is_file() and digest(image) == IMAGE_SHA,
            "real Linux Image SHA mismatch")
    results = (proof / "results.tsv").read_text()
    require(("full_all_verified\t2064\tPASS\t" + IMAGE_SHA) in results,
            "missing full_all_verified independent Image identity")
    verdict = {
        "schema": "minic-stage4-linux2064-archive-receipt-v1",
        "result": "PASS",
        "producer_run": RUN_ID,
        "stage4_selfhost_run": 39058935623,
        "stage4_compiler_sha256": COMPILER_SHA,
        "image_sha256": IMAGE_SHA,
        "owners": 2064,
        "shards": by_shard,
        "unique_verified_objects": len(combined),
        "stage4_real_guest_compiler_invocations": "verified_per_canonical_owner",
        "gnu_preprocessor_assembler_linker": "used_as_original_external_Kbuild_oracle",
        "qemu_p1": "12/12 PASS",
        "recompiled_tus": 0,
        "reran_qemu": False,
    }
    output.write_text(json.dumps(verdict, indent=2) + "\n")
    print("BOOTSTRAP_STAGE4_LINUX2064=PASS stage4_compiler_sha="+COMPILER_SHA+
          " owners=2064/2064 shards=7/7 SHA_verified=true"+
          " QEMU_P1=12/12 Image_sha="+IMAGE_SHA+
          " recompiles=0 replay_QEMU=false", flush=True)


if __name__ == "__main__":
    try:
        cli = argparse.ArgumentParser()
        cli.add_argument("--receiver", required=True, type=Path)
        cli.add_argument("--producers", required=True, type=Path)
        cli.add_argument("--native", required=True, type=Path)
        cli.add_argument("--output", required=True, type=Path)
        a = cli.parse_args()
        run(a.receiver, a.producers, a.native, a.output)
    except (ValueError, OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        print("BOOTSTRAP_STAGE4_LINUX2064=FAIL "+str(error), file=sys.stderr)
        sys.exit(1)
