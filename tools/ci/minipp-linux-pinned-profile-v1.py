#!/usr/bin/env python3
"""Load a pinned RV64 Linux target macro environment without invoking GCC.

Only exact captured option/language contexts are allowed. This is a target-
specific *frozen* GCC ABI compatibility profile, not a general architecture
builtin generator. A profile is promoted only after GNU byte-oracle checks.
"""
from __future__ import annotations

import argparse
import base64
import gzip
import hashlib
import json
from pathlib import Path
import sys


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--snapshot", type=Path, required=True)
    p.add_argument("--language", choices=("c", "assembler-with-cpp"), required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("flags", nargs=argparse.REMAINDER)
    a = p.parse_args()
    flags = a.flags[1:] if a.flags and a.flags[0] == "--" else a.flags
    raw = gzip.decompress(base64.b64decode(a.snapshot.read_text().strip(),
                                           validate=True))
    data = json.loads(raw)
    if data.get("schema") != "minipp-rv64-linux-gcc13-profile-v1":
        raise ValueError("pinned macro snapshot schema mismatch")
    profiles = [x for x in data["profiles"]
                if x["language"] == a.language and x["flags"] == flags]
    if len(profiles) != 1:
        raise ValueError("unvalidated target macros: language=" + a.language
                         + " flags=" + repr(flags)
                         + " matches=" + str(len(profiles)))
    row = profiles[0]
    encoded = row["content"].encode("utf-8")
    if sha(encoded) != row["sha256"] or not encoded:
        raise ValueError("pinned target macro content integrity failure")
    if b"#define __has_attribute(x)" not in encoded or \
            b"#define __has_builtin(x)" not in encoded:
        raise ValueError("missing target feature-query builtin adapters")
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_bytes(encoded)
    if a.output.read_bytes() != encoded:
        raise ValueError("native pinned macro write verification failed")
    print("MINIPP_PINNED_PREDEFINES=PASS language="+a.language
          +" flags="+repr(flags)+" sha="+row["sha256"]
          +" gcc_processes=0 profile_owner_count="+str(row["owner_count"]),
          flush=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print("MINIPP_PINNED_PREDEFINES=FAIL "+str(exc), file=sys.stderr)
        sys.exit(2)
