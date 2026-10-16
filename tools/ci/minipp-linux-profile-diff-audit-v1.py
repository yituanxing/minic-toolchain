#!/usr/bin/env python3
"""Audit the pinned real-Linux RV64 target macro environments.

Read-only: does not build objects, invoke GCC or touch the original Linux tree.
Groups the actual Kbuild contexts by *macro content*, then explains exactly
which macro definitions vary from the common C/assembler reference profiles.
This is the prerequisite for replacing a frozen GCC13 table by MiniPP-owned
architectural/ABI/optimization rules without guessing.
"""
from __future__ import annotations

import argparse
import base64
from collections import defaultdict
import gzip
import hashlib
import json
from pathlib import Path
import re


def digest(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def definitions(text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in text.splitlines():
        if not line.startswith("#define "):
            continue
        match = re.match(r"#define\\s+([A-Za-z_][A-Za-z0-9_]*)(\\([^)]*\\))?(?:\\s+(.*))?$", line)
        if match is None:
            raise ValueError("unparsed predefine: " + line[:180])
        key = match.group(1) + (match.group(2) or "")
        value = match.group(3) or ""
        if key in result and result[key] != value:
            raise ValueError("macro redefinition conflict: " + key)
        result[key] = value
    return result


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--snapshot", type=Path, required=True)
    p.add_argument("--report", type=Path)
    args = p.parse_args()
    packed = "".join(args.snapshot.read_text().split())
    snap = json.loads(gzip.decompress(base64.b64decode(packed, validate=True)))
    if snap.get("schema") != "minipp-rv64-linux-gcc13-profile-v1":
        raise ValueError("unknown snapshot schema")
    rows = snap["profiles"]
    if len(rows) != 9:
        raise ValueError("unexpected actual-Kbuild context inventory")
    for row in rows:
        if digest(row["content"].encode()) != row["sha256"]:
            raise ValueError("profile SHA mismatch")
        row["_macros"] = definitions(row["content"])
    by_language: dict[str, list[dict]] = defaultdict(list)
    for row in rows:
        by_language[row["language"]].append(row)
    if len(by_language["c"]) != 7 or len(by_language["assembler-with-cpp"]) != 2:
        raise ValueError("expected 7 C contexts and 2 assembler contexts")
    c_owners = {owner for row in by_language["c"] for owner in row["owners"]
                if not owner.startswith("dynamic:")}
    d_owners = {owner for row in by_language["c"] for owner in row["owners"]
                if owner.startswith("dynamic:")}
    a_owners = {owner for row in by_language["assembler-with-cpp"]
                for owner in row["owners"]}
    if len(c_owners) != 2064 or len(d_owners) != 4 or len(a_owners) != 34:
        raise ValueError("real Kbuild C/.S/dynamic owner coverage mismatch")

    report = {"schema": "minipp-profile-difference-audit-v1",
              "input_snapshot_sha256": digest(args.snapshot.read_bytes()),
              "gcc_version": snap.get("gcc_version"),
              "profiles": [], "differing_macro_names": []}
    every = defaultdict(set)
    content_groups = defaultdict(list)
    for row in rows:
        content_groups[row["sha256"]].append(row["flags"])
        for name, value in row["_macros"].items():
            every[name].add(value)
    macro_names = sorted(set().union(*(row["_macros"].keys() for row in rows)))
    variable = [name for name in macro_names
                if len({row["_macros"].get(name) for row in rows}) > 1]
    report["differing_macro_names"] = variable
    for lang, subset in sorted(by_language.items()):
        base = max(subset, key=lambda r: r["owner_count"])
        base_macros = base["_macros"]
        for idx, row in enumerate(subset):
            changes = [
                {"name": name,
                 "reference": base_macros.get(name),
                 "actual": row["_macros"].get(name)}
                for name in sorted(set(base_macros) | set(row["_macros"]))
                if base_macros.get(name) != row["_macros"].get(name)
            ]
            entry = {"language": lang, "flags": row["flags"],
                     "sha256": row["sha256"], "owner_count": row["owner_count"],
                     "macro_count": len(row["_macros"]), "delta_from_base": changes}
            report["profiles"].append(entry)
            print("MINIPP_PROFILE_CONTEXT_AUDIT language="+lang+
                  " owners="+str(row["owner_count"])+
                  " flags="+repr(row["flags"])+
                  " macro_sha="+row["sha256"][:16]+
                  " macro_count="+str(len(row["_macros"]))+
                  " base_delta="+str(len(changes)), flush=True)
            for difference in changes[:45]:
                print("MINIPP_PROFILE_MACRO_DELTA language="+lang+
                      " name="+difference["name"]+
                      " base="+repr(difference["reference"])+
                      " actual="+repr(difference["actual"]), flush=True)
            if len(changes) > 45:
                print("MINIPP_PROFILE_MACRO_DELTA_TRUNCATED count="+str(len(changes)-45))
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, sort_keys=True)+"\\n")
    print("MINIPP_PROFILE_AUDIT=PASS contexts=9 unique_macro_profiles="+
          str(len(content_groups))+" C_owners=2064 dynamic_C=4 S_owners=34 "+
          "variable_macro_names="+str(len(variable))+" gcc_invoked=false",
          flush=True)


if __name__ == "__main__":
    main()
