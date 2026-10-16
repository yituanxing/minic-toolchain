#!/usr/bin/env python3
"""Synthesize current RV64 Linux GCC-compatible target macros, without GCC.

For the certified Linux 6.6.143 / GCC 13.3.0 baseline, C macros are derived
from ONE pinned RV64 ABI baseline plus flags governing optimization/PIC/PIE.
The RV32 compat assembler and RV64 assembler currently use separate ABI
baselines; an arbitrary/new -march/-mabi is intentionally rejected.
The historical 9-profile capture remains an independent SHA-checked oracle,
not the per-context production selector. No subprocess/GCC execution.
"""
from __future__ import annotations

import argparse
import base64
import gzip
import hashlib
import json
from pathlib import Path
import re
import sys

PROFILE = Path(__file__).resolve().parents[2] / "tests/preprocessor/profiles/rv64-linux-6.6.143-gcc13.json.gz.b64"

C_COMMON = (
    "-std=gnu11",
    "-fshort-wchar",
    "-funsigned-char",
    "-fno-PIE",
    "-mabi=lp64",
    "-march=rv64imac_zicsr_zifencei_zihintpause",
    "-mcmodel=medany",
)
C_OPTIONS = {"-O2", "-fno-builtin-wcslen", "-fPIC", "-fpic", "-fpie"}
S64 = (
    "-fno-PIE", "-mabi=lp64",
    "-march=rv64imafdcv_zicsr_zifencei_zihintpause",
)
S32_SUFFIX = ("-march=rv32g", "-mabi=ilp32")


def load_snapshot(path: Path) -> dict:
    packed = "".join(path.read_text().split())
    obj = json.loads(gzip.decompress(base64.b64decode(packed, validate=True)))
    if obj.get("schema") != "minipp-rv64-linux-gcc13-profile-v1" or obj.get("gcc_version") != "13.3.0":
        raise ValueError("unrecognized frozen GNU13/RV64 compatibility oracle")
    if len(obj.get("profiles", [])) != 9:
        raise ValueError("unexpected number of frozen compiler profiles")
    for row in obj["profiles"]:
        if hashlib.sha256(row["content"].encode()).hexdigest() != row["sha256"]:
            raise ValueError("pinned compiler oracle SHA mismatch")
    return obj


def macro_map(text: str) -> dict[str, str]:
    macros = {}
    for line in text.splitlines():
        if line.startswith("#undef "):
            macros.pop(line[7:].strip(), None)
        elif line.startswith("#define "):
            match = re.fullmatch(
                r"#define\s+([a-zA-Z_][a-zA-Z_0-9]*)(\([^)]*\))?(?:\s+(.*))?", line)
            if match is None:
                raise ValueError("invalid target macro definition: " + line)
            macros[match.group(1) + (match.group(2) or "")] = match.group(3) or ""
        elif line.strip():
            raise ValueError("unexpected data in pinned target macro profile")
    return macros


def synth(snapshot: dict, language: str, flags: list[str]) -> tuple[str, str]:
    rows = snapshot["profiles"]
    if language == "c":
        if len(flags) < len(C_COMMON) or tuple(flags[:len(C_COMMON)]) != C_COMMON:
            raise ValueError("unsupported C ABI, option order, or optimization prelude: " + repr(flags))
        extras = flags[len(C_COMMON):]
        if any(x not in C_OPTIONS and x != "-mcmodel=medany" for x in extras):
            raise ValueError("unsupported C target or unclassified builtin flag: " + repr(extras))
        if len([x for x in extras if x in {"-fPIC", "-fpic", "-fpie"}]) > 1:
            raise ValueError("conflicting PIC/PIE flags")
        if extras.count("-O2") > 1 or extras.count("-fno-builtin-wcslen") > 1:
            raise ValueError("duplicate optimization or builtin option")
        base = next(x for x in rows
                    if x["language"] == "c" and x["flags"] == list(C_COMMON))
        additional = []
        if "-O2" in extras:
            additional.extend(("#undef __NO_INLINE__", "#define __OPTIMIZE__ 1",
                               "#define _FORTIFY_SOURCE 3"))
        if "-fPIC" in extras:
            additional.extend(("#define __PIC__ 2", "#define __pic__ 2"))
        if "-fpic" in extras:
            additional.extend(("#define __PIC__ 1", "#define __pic__ 1"))
        if "-fpie" in extras:
            additional.extend(("#define __PIC__ 1", "#define __pic__ 1",
                               "#define __PIE__ 1", "#define __pie__ 1"))
        return base["content"] + "".join(x + "\n" for x in additional), "rv64-lp64-c-common+explicit-flag-rules"
    if language == "assembler-with-cpp":
        actual = tuple(flags)
        if actual == S64:
            row = next(x for x in rows
                       if x["language"] == "assembler-with-cpp" and x["flags"] == list(S64))
            return row["content"], "rv64-lp64-assembler-ABI"
        if actual == S64 + S32_SUFFIX:
            row = next(x for x in rows if x["language"] == "assembler-with-cpp"
                       and x["flags"] == list(S64 + S32_SUFFIX))
            return row["content"], "rv32-ilp32-assembler-ABI"
        raise ValueError("unsupported architecture/ABI for assembler: " + repr(flags))
    raise ValueError("unknown preprocessor language " + language)


def check(snapshot: dict) -> None:
    unique = set()
    for row in snapshot["profiles"]:
        actual, policy = synth(snapshot, row["language"], row["flags"])
        if macro_map(actual) != macro_map(row["content"]):
            expected = macro_map(row["content"])
            got = macro_map(actual)
            diff = sorted(x for x in set(expected) | set(got)
                          if expected.get(x) != got.get(x))
            raise ValueError("native target macros disagree with frozen GCC13: "
                             + repr(row["flags"]) + " macros=" + repr(diff[:20]))
        unique.add(row["sha256"])
        print("MINIPP_NATIVE_MACRO_RULE=PASS lang="+row["language"]+
              " owners="+str(row["owner_count"])+" rules="+policy+
              " macros="+str(len(macro_map(actual))), flush=True)
    print("MINIPP_NATIVE_MACRO_RULES=PASS contexts=9 unique_reference_profiles="+
          str(len(unique))+" native_c_rule_contexts=7 native_s_abi_contexts=2 "+
          "gcc_processes=0 exact_macro_dictionary=true", flush=True)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--snapshot", type=Path, default=PROFILE)
    p.add_argument("--self-check", action="store_true")
    p.add_argument("--language", choices=("c", "assembler-with-cpp"))
    p.add_argument("--output", type=Path)
    p.add_argument("flags", nargs=argparse.REMAINDER)
    a = p.parse_args()
    snapshot = load_snapshot(a.snapshot)
    if a.self_check:
        check(snapshot)
        return
    if not a.language or a.output is None:
        raise ValueError("provide --language and --output, or --self-check")
    flags = a.flags[1:] if a.flags and a.flags[0] == "--" else a.flags
    body, policy = synth(snapshot, a.language, flags)
    if not body:
        raise ValueError("empty target macro profile")
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(body)
    print("MINIPP_NATIVE_MACRO_PROFILE=PASS language="+a.language+
          " rules="+policy+" target_macros="+str(len(macro_map(body)))+
          " gcc_processes=0", flush=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print("MINIPP_NATIVE_MACRO_RULES=FAIL "+str(exc), file=sys.stderr)
        sys.exit(2)
