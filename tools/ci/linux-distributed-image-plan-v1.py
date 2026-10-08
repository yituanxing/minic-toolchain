#!/usr/bin/env python3
"""Extract exact ordered C translation unit object graph from real Linux 6.6.143 Kbuild -n Image."""
import hashlib
import pathlib
import re
import sys

if len(sys.argv) != 5:
    raise SystemExit("usage: plan.py <kbuild-plan.raw> <out-tree> <manifest.tsv> <shards:7>")
plan = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2]).resolve()
dest = pathlib.Path(sys.argv[3])
nshards = int(sys.argv[4])
assert nshards == 7
obj_re = re.compile(r"(?:^|\s)-o\s+([^\s;]+\.o)(?=\s|;|$)")
src_re = re.compile(r"(?:^|\s)([^\s;]+\.c)(?=\s|;|$)")
seen = set()
entries = []
for line in plan.read_text(errors="replace").splitlines():
    if "riscv64-linux-gnu-" not in line or " -c " not in " " + line + " ":
        continue
    sources = src_re.findall(line)
    objects = obj_re.findall(line)
    if not sources or not objects:
        continue
    obj = pathlib.Path(objects[-1].strip("'\""))
    # Linux Kbuild deliberately spells some shared source objects through paths
    # like arch/riscv/kvm/../../../virt/kvm/kvm_main.o. Canonicalize *inside*
    # out-tree before duplicate detection rather than rejecting valid .. paths.
    target = obj.resolve() if obj.is_absolute() else (out / obj).resolve()
    try:
        obj = target.relative_to(out)
    except ValueError:
        raise SystemExit(f"DIST_IMAGE_OBJECT_ESCAPE {target}")
    rel = obj.as_posix().removeprefix("./")
    if not rel.endswith(".o") or rel.startswith(("scripts/", "tools/")) or "/scripts/" in rel:
        continue
    if ".." in pathlib.PurePosixPath(rel).parts:
        raise SystemExit("DIST_IMAGE_BAD_TARGET " + rel)
    if rel in seen:
        continue
    seen.add(rel)
    entries.append((rel, sources[-1].strip("'\"")))
if len(entries) < 2500 or len(entries) > 6000:
    raise SystemExit(f"DIST_IMAGE_PLAN_INCOMPLETE targets={len(entries)} (expected roughly 3352)")
dest.parent.mkdir(parents=True, exist_ok=True)
with dest.open("w") as f:
    for i, (obj, src) in enumerate(entries):
        f.write(f"{i}\t{i % nshards}\t{obj}\t{src}\n")
digest = hashlib.sha256(dest.read_bytes()).hexdigest()
(dest.parent / "manifest.sha256").write_text(digest + "\n")
for i in range(nshards):
    print(f"DIST_IMAGE_PLAN_SHARD id={i} count={sum(1 for j in range(len(entries)) if j % nshards == i)}")
print(f"DIST_IMAGE_PLAN=PASS total={len(entries)} sha256={digest}")
