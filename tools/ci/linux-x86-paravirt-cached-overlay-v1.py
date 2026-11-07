#!/usr/bin/env python3
"""Rebuild only old x86 C owners with undersized PV replacement sites.

Consumes the immutable original GNU -E/.cmd ZIPs from #42366441631 and
MiniC/GNU-AS object ZIPs from #42384023426. Every immutable artifact and all
2758 source/object owner hashes are authenticated before replacing any owner.
The replacement is generated only by current MiniC -S plus original GNU AS.

This command does NOT claim Linux boot success; its result is a SHA-bearing,
small overlay for a separate original GNU Kbuild LD + QEMU P0/P1 receiver.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import zipfile


INPUT_SHAS = {
    0: "c705b5f25a384f8d5d9dfedec1926a71619afa6336759a8bfebf1eb7faf73e76",
    1: "c061ec9ca219f64cad91bda509de2bf48e2f8d7f0fadb3c4fbe9baeef2a29c6d",
    2: "f7855ec96b9aeb43fd6921c7b5e5ca2da530046599bd31d60a7ad0cd808dd279",
    3: "cd69c5b083f679249c8891addf77a8f71494c51475e4eac8b2957bf7d3e17f9e",
    4: "c5a148c14128ab22ebbd79dd090a32de3bd5a41ca95306f9b3b9c48ee8e1df80",
    5: "3ea43edbf23d24f5ca980701a0d69eeedcd3c7f66a2bad4565449352c95df9bb",
}
OBJECT_SHAS = {
    0: "28c14a6913ee4a6c1048db5807e9176872f7ac9f73cb04bcabeda6fb96c1a8af",
    1: "c6f83ef88875bf3ae2b46883ee018da27ae7ca4b94474ec2dd1ca8ca6a07673a",
    2: "543269f18a29e7f00856d27592285ee6734755ce6edd4ca5b10bbf83aba6bd76",
    3: "39d4a5bf6a59c23bcc4d66534bef501c8491a829c3a8e538a345703a2693dda7",
    4: "a89f0bad20f754c1b186f4e168ca28385d39dfdf8eaae0f19403adc39a020f81",
    5: "53bd19692b99f684c6c589dbb7e4334471b34a7a145fae1e4510281dc8c75679",
    6: "5c94275047b9d590fda15575748eb7c1ab643b5d2f7d4dbc0507c87828cf198c",
}
FROZEN_SHA = "9e666a64061901a9fb980bbb3710cd3b0a8cc6c35fb61be118646463d191352a"
# Includes all expected .parainstructions C owners; never silently enlarge.
EXPECTED_SITE_COUNTS = {
    "arch/x86/kernel/process.o": 1,
    "arch/x86/mm/tlb.o": 4,
    "drivers/tty/serial/8250/8250_port.o": 1,
    "arch/x86/mm/extable.o": 1,
    "mm/mmap.o": 1,
    "arch/x86/mm/pgtable.o": 1,
    "drivers/i2c/busses/i2c-i801.o": 1,
    "drivers/acpi/processor_idle.o": 2,
    "drivers/video/console/vgacon.o": 1,
    "arch/x86/kernel/reboot.o": 1,
    "drivers/clocksource/i8253.o": 1,
    "drivers/usb/host/pci-quirks.o": 1,
}


def sha(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def file_sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for piece in iter(lambda: f.read(1024 * 1024), b""):
            h.update(piece)
    return h.hexdigest()


def valid(condition: bool, error: str) -> None:
    if not condition:
        raise ValueError(error)


def elf_paravirt_sites(elf: bytes) -> list[tuple[int, int]]:
    """(PV type, site byte length) from *actual* ELF section bytes."""
    valid(elf[:6] == b"\x7fELF\x02\x01", "not ELF64 little-endian")
    shoff = struct.unpack_from("<Q", elf, 40)[0]
    stride, num, index = struct.unpack_from("<HHH", elf, 58)
    strhdr = shoff + index * stride
    start, count = struct.unpack_from("<QQ", elf, strhdr + 24)
    strings = elf[start : start + count]
    result = []
    for i in range(num):
        hdr = shoff + i * stride
        n = struct.unpack_from("<I", elf, hdr)[0]
        end = strings.find(b"\x00", n)
        if strings[n:end] != b".parainstructions":
            continue
        off, size = struct.unpack_from("<QQ", elf, hdr + 24)
        valid(size % 16 == 0, "malformed .parainstructions record width")
        payload = elf[off : off + size]
        valid(len(payload) == size, "truncated .parainstructions")
        result.extend((payload[j + 8], payload[j + 9])
                      for j in range(0, size, 16))
    return result


def module(path: str, name: str):
    src = Path(__file__).with_name(path)
    spec = importlib.util.spec_from_file_location(name, src)
    valid(spec is not None and spec.loader is not None,
          f"missing original seven-runner code: {src}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input-zip-dir", required=True, type=Path)
    p.add_argument("--object-zip-dir", required=True, type=Path)
    p.add_argument("--minic-cc", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--workers", default=3, type=int)
    p.add_argument("--timeout", default=150, type=int)
    a = p.parse_args()
    valid(not a.output.exists(), "refuse stale overlay destination")
    c = module("linux-three-isa-minic-candidate-v1.py", "cached_pv_candidate")
    blobs = {}
    for shard, pinned in INPUT_SHAS.items():
        path = a.input_zip_dir / f"input-shard{shard}.zip"
        valid(file_sha(path) == pinned, f"official frozen input ZIP SHA mismatch {shard}")
        blobs[("input", shard)] = zipfile.ZipFile(path)
    for shard, pinned in OBJECT_SHAS.items():
        path = a.object_zip_dir / f"shard{shard}.zip"
        valid(file_sha(path) == pinned, f"official original candidate ZIP SHA mismatch {shard}")
        blobs[("object", shard)] = zipfile.ZipFile(path)
    manifest_raw = blobs[("input", 4)].read("frozen/manifest.json")
    valid(sha(manifest_raw) == FROZEN_SHA, "original GNU frozen manifest SHA mismatch")
    manifest = json.loads(manifest_raw)
    rows = manifest["rows"]
    valid(len(rows) == 2758 and all(row["index"] == i and row["shard"] == i % 7
                                    for i, row in enumerate(rows)),
          "frozen 2758 C-owner inventory is not exact")
    by_name = {row["object"]: row for row in rows}
    valid(len(by_name) == len(rows), "duplicate frozen C owner")
    old_sites = {}
    old_sha = {}
    for shard in range(7):
        z = blobs[("object", shard)]
        ident = json.loads(z.read("identity.json"))
        valid(ident["shard"] == shard and ident["target"] == "x86_64" and
              ident["total_c_owners"] == 2758 and
              ident["frozen_manifest_sha256"] == FROZEN_SHA,
              f"original candidate object shard identity mismatch {shard}")
        results = json.loads(z.read("results.json"))
        valid(len(results) == len(rows[shard::7]), "original candidate shard incomplete")
        for row, result in zip(rows[shard::7], results):
            name = row["object"]
            valid(result["object"] == name and
                  result["input_sha256"] == row["sha256"] and
                  result["status"] == "MINIC_S_GNU_AS_PASS",
                  f"old owner result mismatch: {name}")
            obj = z.read("objects/" + name)
            digest = sha(obj)
            valid(result["obj_sha256"] == digest,
                  f"old owner object SHA mismatch: {name}")
            old_sha[name] = digest
            sites = elf_paravirt_sites(obj)
            if sites:
                old_sites[name] = sites
    valid(set(old_sites) == set(EXPECTED_SITE_COUNTS) and
          all(len(old_sites[name]) == EXPECTED_SITE_COUNTS[name] and
              all(length == 3 for _typ, length in old_sites[name])
              for name in old_sites),
          "the historical 12-owner / 16-site paravirt bug is not identical")
    print("PV_ORIGINAL_SEVEN=SHA_PASS owners=2758 pv_owners=12 sites=16 all_len=3", flush=True)

    # Stage exactly the original 12 source files and original .cmd; owner
    # names/metadata/bytes come from frozen GNU, never from working tree C.
    workspace = a.output / "owner-replay"
    frozen = workspace / "frozen"
    work_build = workspace / "build"
    work_build.mkdir(parents=True)
    (frozen / "manifest.json").parent.mkdir(parents=True, exist_ok=True)
    (frozen / "manifest.json").write_bytes(manifest_raw)
    for name in EXPECTED_SITE_COUNTS:
        row = by_name[name]
        z = blobs[("input", row["shard"])]
        valid(z.read("frozen/manifest.json") == manifest_raw,
              f"source shard frozen manifest drift {row['shard']}")
        source = z.read("frozen/" + row["input"])
        valid(sha(source) == row["sha256"], f"frozen GNU .i SHA mismatch: {name}")
        stem = Path(name)
        cmd_path = str(stem.parent / ("." + stem.name + ".cmd"))
        cmd = z.read("build/" + cmd_path)
        valid(sha(cmd) == row["cmd_sha256"],
              f"original GNU Kbuild .cmd SHA mismatch: {name}")
        src = frozen / row["input"]
        src.parent.mkdir(parents=True, exist_ok=True)
        src.write_bytes(source)
        path = work_build / cmd_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(cmd)
    valid(sum(EXPECTED_SITE_COUNTS.values()) == 16,
          "incorrect paravirt site inventory")
    os.environ["TMPDIR"] = str((workspace / "temporary").resolve())
    Path(os.environ["TMPDIR"]).mkdir(parents=True, exist_ok=True)
    changed = [by_name[name] for name in EXPECTED_SITE_COUNTS]
    result_dir = a.output / "new-objects"
    data = {"target": "x86_64", "gcc": "gcc", "machine": 62}
    results = c.make_objects(changed, workspace, a.minic_cc.resolve(),
                             result_dir, data, a.workers, a.timeout)
    receipt = {"schema": "minic-x86-paravirt-cached-overlay-v1",
               "original_gnu_source_run": 42366441631,
               "original_minic_object_run": 42384023426,
               "old_c_owners": 2758, "affected_owners": 12,
               "old_undersized_sites": 16,
               "frozen_manifest_sha256": FROZEN_SHA,
               "new_compiler_sha256": file_sha(a.minic_cc.resolve()),
               "status": "MINIC_S_GNU_AS_OVERLAY_NOT_RUNTIME_CERTIFIED",
               "objects": []}
    for row, result in zip(changed, results):
        name = row["object"]
        valid(result["status"] == "MINIC_S_GNU_AS_PASS",
              f"new MiniC -S plus GNU AS failed: {name}: {result}")
        obj = result_dir / "staged" / name
        valid(file_sha(obj) == result["obj_sha256"],
              f"new object SHA drift: {name}")
        sites = elf_paravirt_sites(obj.read_bytes())
        valid(len(sites) == EXPECTED_SITE_COUNTS[name] and
              all(length >= 5 for _typ, length in sites),
              f"replacement site length still invalid: {name}: {sites}")
        receipt["objects"].append({"object": name, "shard": row["shard"],
            "frozen_i_sha256": row["sha256"],
            "original_cmd_sha256": row["cmd_sha256"],
            "previous_obj_sha256": old_sha[name],
            "new_obj_sha256": result["obj_sha256"],
            "new_asm_sha256": result["asm_sha256"],
            "original_site_lengths": [length for _typ, length in old_sites[name]],
            "new_site_lengths": [length for _typ, length in sites]})
        print(f"PV_OVERLAY_OWNER=PASS {name} sites={len(sites)} "
              f"old=3 new={min(length for _typ, length in sites)}", flush=True)
    assert len(receipt["objects"]) == 12
    (a.output / "overlay-receipt.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    print("PV_CACHED_OVERLAY=PASS original_owners=2758 recompiled=12 "
          "site_len_min=5 full_kernel_QEMU=NOT_YET_RUN", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as exc:
        sys.exit(f"PV_CACHED_OVERLAY=FAIL reason={exc}")
