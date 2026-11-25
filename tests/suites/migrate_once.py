#!/usr/bin/env python3
"""One-shot five-round migration, preserving original bytes/modes per case."""
import collections
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from materialize import ROOT, SUITES, MARK, END, inventory

BASE = "4f3b6b1b087e4e9cb8a0d17fdac5b8a46d193bad"
os.chdir(ROOT)

def git(*args):
    return subprocess.check_output(["git", *args])

def tree(ref):
    result = {}
    for entry in git("ls-tree", "-rz", ref, "tests").split(b"\0"):
        if not entry:
            continue
        meta, path = entry.split(b"\t", 1)
        mode, typ, sha = meta.decode().split()
        if typ == "blob":
            result[path.decode()] = (mode, sha)
    return result

original = tree(BASE)
removed = set()
stats = []
def read(path):
    return Path(path).read_bytes()

def record(meta, body):
    if any(line.startswith(MARK) for line in body.splitlines()):
        raise ValueError("reserved section marker occurs in input")
    meta = dict(meta, newline=body.endswith(b"\n"))
    return MARK + json.dumps(meta, ensure_ascii=True, sort_keys=True).encode() + b"\n" + body + (b"" if body.endswith(b"\n") else b"\n") + END

def semantic(path):
    stem = Path(path).stem
    # group by existing module and semantic name, not by a numerical archive
    stem = re.sub(r"^(?:run|probe|check|invalid|reject)[_-]", "", stem)
    part = re.split(r"[-_]", stem)[0].lower()
    if re.fullmatch(r"(?:mc|m|p)\d+", part):
        part = "milestones"
    return re.sub(r"[^a-z0-9]+", "-", part)[:25] or "other"

def eligible(p, suffixes, dirs):
    return (p not in removed and Path(p).suffix in suffixes and
            any(p.startswith(d + "/") for d in dirs) and
            original[p][0] in ("100644","100755") and
            b"\0" not in read(p) and
            not any(x.startswith(MARK) for x in read(p).splitlines()))

def fixture_phase(tag, dirs, suffixes):
    groups = collections.defaultdict(list)
    for p in sorted(original):
        if eligible(p, suffixes, dirs):
            read(p).decode("utf-8")
            area = "c0" if p.startswith("tests/compiler/c0/") else p.split("/")[1]
            groups[area, semantic(p)].append(p)
    # Preserve semantic categories with >= 4 cases; combine small categories in
    # each area, with explicit path names and independently editable inputs.
    merged = collections.defaultdict(list)
    for (area, family), paths in groups.items():
        merged[area, family if len(paths) >= 4 else "misc"].extend(paths)
    for (area, family), paths in sorted(merged.items()):
        chunk = []
        size = 0
        count = 0
        for p in sorted(paths):
            body = read(p)
            rec = record({"kind":"case","path":p,"mode":original[p][0]}, body)
            if chunk and (size + len(rec) > 160000 or len(chunk) >= 80):
                count += 1
                write_suite(f"{tag}-{area}-{family}-{count:02}", chunk)
                chunk, size = [], 0
            chunk.append((p, rec))
            size += len(rec)
        if chunk:
            write_suite(f"{tag}-{area}-{family}-{count+1:02}", chunk)

def write_suite(name, chunk, prefix=b""):
    dest = SUITES / (name + ".cases")
    if dest.exists():
        raise ValueError(f"duplicate suite {dest}")
    dest.write_bytes(prefix + b"".join(rec for _,rec in chunk))
    for p,_ in chunk:
        Path(p).unlink()
        removed.add(p)

def shell_prefix(p):
    body = read(p)
    lines = body.splitlines(keepends=True)
    if not lines or lines[0].strip() not in (b"#!/bin/sh",b"#!/bin/bash",b"#!/usr/bin/env bash"):
        return None
    i = 1
    while i < len(lines) and (not lines[i].strip() or lines[i].startswith(b"#")):
        i += 1
    if i >= len(lines) or lines[i].strip() not in (b"set -eu", b"set -e", b"set -Eeuo pipefail", b"set -euo pipefail"):
        return None
    j = i+1
    while j < min(len(lines), i+14):
        line = lines[j].strip()
        if not line or line.startswith(b"#") or line.endswith(b"\\") or b"<<" in line:
            break
        if not re.match(rb"(?:[A-Za-z_][A-Za-z0-9_]*=|trap |test |: |mkdir -p |command -v )",line):
            break
        j += 1
    return lines, i, j

def script_phase(tag, dirs, minimum):
    groups = collections.defaultdict(list)
    for p in sorted(original):
        if not eligible(p,{".sh"},dirs):
            continue
        body = read(p)
        body.decode("utf-8")
        info = shell_prefix(p)
        if not info:
            continue
        lines, i,j = info
        if j-i < minimum:
            continue
        area = "c0" if p.startswith("tests/compiler/c0/") else p.split("/")[1]
        groups[area,b"".join(lines[i:i+minimum])].append((p,lines,i,j))
    for (area,prefix), cases in sorted(groups.items()):
        if len(cases) < 3:
            continue
        n = minimum
        while all(j-i>n for _,_,i,j in cases) and len({lines[i+n] for _,lines,i,j in cases})==1:
            prefix += cases[0][1][cases[0][2]+n]
            n+=1
        ident = hashlib.sha256(prefix).hexdigest()[:16]
        head = record({"kind":"shared","id":ident}, prefix)
        chunk = []
        size = len(head)
        count = 1
        for p,lines,i,j in cases:
            body=b"".join(lines[:i]+lines[i+n:])
            rec=record({"kind":"case","path":p,"mode":original[p][0],"shared":ident,"head_lines":i},body)
            if chunk and (size+len(rec)>180000 or len(chunk)>=75):
                write_suite(f"{tag}-{area}-{ident}-{count:02}",chunk,head)
                count+=1
                chunk,size=[],len(head)
            chunk.append((p,rec))
            size+=len(rec)
        if chunk:
            write_suite(f"{tag}-{area}-{ident}-{count:02}",chunk,head)

def verify():
    outputs=inventory()
    assert set(outputs)==removed,(len(outputs),len(removed))
    for p,(b,mode) in outputs.items():
        oldmode,sha=original[p]
        blob=hashlib.sha1(b"blob "+str(len(b)).encode()+b"\0"+b).hexdigest()
        assert blob==sha and int(oldmode[-3:],8)==mode, p
    for p in original.keys()-removed:
        assert Path(p).is_file(),p
    return len(outputs)

def commit_round(n, message):
    count=verify()
    subprocess.run(["git","add","-A","--","tests"],check=True)
    subprocess.run(["git","commit","-m",f"test suite {n}/5: {message}"],check=True)
    actual=len(tree("HEAD"))
    stats.append({"round":n,"tracked_tests":actual,"generated_cases":count,"canonical_suites":len(list(SUITES.glob("*.cases")))})
    print("SUITE_ROUND",json.dumps(stats[-1]),flush=True)

# R1: C0 translation units, preserving separate C compilation.
fixture_phase("c0-input",["tests/compiler/c0"],{".c",".h",".i",".s",".S"})
commit_round(1,"organize C0 translation units into named semantic suites")
# R2: other compiler/target tests; keep upstream and external projects independent.
fixture_phase("input",["tests/compiler/audit","tests/compiler/mir-migration","tests/core","tests/frontend","tests/programs","tests/target"],{".c",".h",".i",".s",".S"})
commit_round(2,"unify target, frontend, Core and program input organization")
# R3: substantial exact setup blocks; leave all differing assertions unchanged.
script_phase("driver",["tests/compiler/c0","tests/target"],4)
commit_round(3,"share identical multi-command regression driver setup")
# R4: expand shared-driver families; only two or more substantive shared lines
# beyond the shell mode, at least three members; do not bundle unique scripts.
script_phase("driver-common",["tests/compiler/c0","tests/target","tests/compiler/audit","tests/compiler/mir-migration","tests/core","tests/frontend","tests/programs"],3)
commit_round(4,"consolidate remaining proven common test setup families")
# R5: explicit compatibility integration; original test paths remain generated.
ignore=Path(".gitignore")
with ignore.open("a") as f:
    f.write("\n# Generated named test cases; edit canonical tests/suites/*.cases instead.\n")
    for p in sorted(removed):
        f.write("/"+p+"\n")
make=Path("Makefile")
s=make.read_text()
anchor="SHELL := /bin/sh\n"
assert s.count(anchor)==1
s=s.replace(anchor,anchor+"\n# Test input suites generate legacy paths before dependency resolution.\nifneq ($(filter check% test% sanitize,$(MAKECMDGOALS)),)\nMINIC_TEST_SUITE_STATUS := $(shell python3 tests/suites/materialize.py >&2; echo $$?)\nifneq ($(MINIC_TEST_SUITE_STATUS),0)\n$(error Test suite materialization failed)\nendif\nendif\n",1)
s+="\n.PHONY: prepare-tests\nprepare-tests:\n\tpython3 tests/suites/materialize.py\n"
make.write_text(s)
(SUITES/"README.md").write_text("""# Canonical named test suites

These are editable, plain-text test definitions, not binary archives.
Each case section has its original independent translation unit or shell
body. Driver suites factor out exact common command blocks. Different
assertions and GNU/ABI/QEMU checks remain independent. No test is removed.

Run \`make prepare-tests\` after checkout before directly invoking legacy
test paths. Make check targets automatically expand the inputs. Edit
the canonical .cases source rather than generated files; use
\`python3 tests/suites/materialize.py --refresh\` after editing.
\`--list\` lists the individual named cases.

The compatibility paths are generated and ignored by Git. Historical
CI that inspects the Git tree, rather than the filesystem, requires a
separate adaptation. This branch is a candidate until that is verified.
""".replace("\\",""))
verify()
subprocess.run(["git","add","-A","--","tests",".gitignore","Makefile"],check=True)
subprocess.run(["git","commit","-m","test suite 5/5: integrate named case materializer and local test entrypoints"],check=True)
stats.append({"round":5,"tracked_tests":len(tree("HEAD")),"generated_cases":len(removed),"canonical_suites":len(list(SUITES.glob("*.cases")))})
print("SUITE_ROUND",json.dumps(stats[-1]),flush=True)
print("SUITE_FINAL",json.dumps(stats),flush=True)
print("SUITE_SOURCE_REDUCTION",sum(len(read(p).splitlines()) for p in original.keys()-removed if Path(p).suffix==".sh"),"remaining standalone script lines",flush=True)
