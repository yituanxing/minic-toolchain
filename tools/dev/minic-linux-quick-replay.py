#!/usr/bin/env python3
"""Fast frozen-GNU Linux .i A/B replay. NOT AS/link/QEMU certification.

Use --corpus ARCH=gnu-i.zip, --owner ARCH:path.o, --minic new/minic-cc
and optionally --before old/minic-cc. Only selected SHA-checked .i files
are extracted; both compilers receive identical original Kbuild ABI flags.
Use --roots three-linux-all-root-causes.json --top 20 to select one owner
per top diagnostic class, avoiding a 21-shard rescan for each code fix.
"""
import argparse
import concurrent.futures as futures
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zipfile

ARCHES = ("x86_64", "aarch64", "loongarch64")
FLAGS = set("-fshort-wchar -fno-short-wchar -fsigned-char -funsigned-char -fno-signed-char -fno-unsigned-char -ffunction-sections -fno-function-sections -fPIE -fpie -fPIC -fpic -fno-PIE -fno-pie -fno-PIC -fno-pic".split())
ERR = re.compile(r"[^ :]+\.(?:i|c|h|s):\d+(?::\d+)?:")
NAME = re.compile(r"FRONTEND_LOOKUP_DETAIL name=([^ ]+)")

def sha(data):
    return hashlib.sha256(data).hexdigest()

def load(archive, arch, owners, samples):
    with zipfile.ZipFile(archive) as z:
        manifest = json.loads(z.read("frozen/manifest.json"))
        if manifest.get("schema") != "three-isa-gnu-i-v1" or manifest.get("errors") or manifest["identity"]["target"] != arch:
            raise ValueError("invalid GNU .i archive for " + arch)
        index = {r["object"]: r for r in manifest["rows"]}
        if len(index) != len(manifest["rows"]):
            raise ValueError("duplicate frozen GNU objects")
        names = sorted(owners) if owners else sorted(index)[:samples]
        tasks = []
        for name in names:
            if name not in index:
                raise ValueError("unknown original GNU owner " + name)
            row = index[name]
            member = "frozen/" + row["input"]
            if not member.startswith("frozen/i/") or ".." in Path(member).parts:
                raise ValueError("unsafe frozen input")
            flags = row["minic_flags"]
            if not isinstance(flags, list) or any(f not in FLAGS for f in flags):
                raise ValueError("invalid original Kbuild MiniC flags")
            content = z.read(member)
            if sha(content) != row["sha256"]:
                raise ValueError("original GNU input SHA mismatch " + name)
            tasks.append((arch, name, row["index"], flags, content))
        return tasks

def run(task, tag, compiler, tempdir, timeout):
    arch, name, index, flags, content = task
    d = tempdir / tag / arch / str(index)
    d.mkdir(parents=True, exist_ok=True)
    src, asm = d / "source.i", d / "source.s"
    src.write_bytes(content)
    env = os.environ.copy()
    env["CORE_FAST_TRACE"] = "1"
    try:
        proc = subprocess.run([str(compiler), "--target=" + arch + "-linux",
                               *flags, "-S", str(src), "-o", str(asm)],
                              stderr=subprocess.PIPE, stdout=subprocess.DEVNULL,
                              env=env, timeout=timeout)
        log = proc.stderr.decode(errors="replace")
        good = proc.returncode == 0 and asm.is_file() and asm.stat().st_size > 0
        errors = [line for line in log.splitlines() if "error:" in line.lower()]
        refs = NAME.findall(log)
        return dict(status="S_PASS" if good else "S_FAIL",
                    diagnostic=ERR.sub("<source>:", errors[0])[:220] if errors else "",
                    undeclared=refs[-1] if refs else "",
                    asm_sha256=sha(asm.read_bytes()) if good else None)
    except subprocess.TimeoutExpired:
        return dict(status="S_TIMEOUT", diagnostic="timeout", undeclared="", asm_sha256=None)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--corpus", action="append", default=[], help="ARCH=gnu-i.zip")
    p.add_argument("--owner", action="append", default=[], help="ARCH:owner.o")
    p.add_argument("--roots", type=Path, help="attested 21-shard root-cause JSON")
    p.add_argument("--top", type=int, default=20)
    p.add_argument("--sample", type=int, default=0)
    p.add_argument("--minic", type=Path, required=True)
    p.add_argument("--before", type=Path)
    p.add_argument("--workers", type=int, default=4)
    p.add_argument("--timeout", type=int, default=80)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    if not 1 <= args.workers <= 32 or not 1 <= args.timeout <= 3600:
        p.error("invalid worker count or timeout")
    archives, selected = {}, {a:set() for a in ARCHES}
    for item in args.corpus:
        arch, sep, path = item.partition("=")
        if not sep or arch not in ARCHES or arch in archives:
            p.error("invalid or duplicate ARCH=gnu-i.zip")
        archives[arch] = Path(path)
    if args.roots:
        roots = json.loads(args.roots.read_text())
        if roots.get("schema") != "minic-linux-cross-target-diagnostic-census-v2" or not 1 <= args.top <= 1000:
            p.error("invalid root census or --top")
        for group in roots["diagnostic_groups"][:args.top]:
            for arch, names in group["targets"].items():
                if arch in archives and names:
                    selected[arch].add(names[0])
    for owner in args.owner:
        arch, sep, name = owner.partition(":")
        if not sep or arch not in archives or not name.endswith(".o"):
            p.error("invalid ARCH:owner.o")
        selected[arch].add(name)
    tasks = []
    for arch, archive in archives.items():
        if selected[arch] or args.sample:
            tasks.extend(load(archive, arch, selected[arch], args.sample))
    tasks.sort(key=lambda t:(t[0],t[2]))
    if not tasks:
        p.error("select owners with --owner, --roots, or --sample")
    new = args.minic.resolve(strict=True)
    old = args.before.resolve(strict=True) if args.before else None
    with tempfile.TemporaryDirectory(prefix="minic-frozen-ab-") as td:
        temp = Path(td)
        work = [(t, label, cc) for t in tasks for label, cc in
                ([("after", new), ("before", old)] if old else [("after", new)])]
        with futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
            receipts = list(pool.map(lambda x: (x[0][0], x[0][1], x[1],
                                                 run(x[0], x[1], x[2], temp, args.timeout)), work))
    got = {(a,b,label):r for a,b,label,r in receipts}
    results = []
    regressed = 0
    for arch, name, index, flags, content in tasks:
        after = got[arch,name,"after"]
        before = got.get((arch,name,"before"))
        if before and before["status"] == "S_PASS" and after["status"] != "S_PASS":
            delta = "REGRESSED"
            regressed += 1
        elif before and before["status"] != "S_PASS" and after["status"] == "S_PASS":
            delta = "IMPROVED"
        elif before and before["status"] != "S_PASS" and after["status"] != "S_PASS" and (before["diagnostic"],before["undeclared"]) != (after["diagnostic"],after["undeclared"]):
            delta = "FRONTIER_MOVED"
        else:
            delta = "UNCHANGED" if before else "OBSERVED"
        results.append(dict(target=arch, owner=name, source_sha256=sha(content),
                            flags=flags, after=after, before=before, delta=delta))
        print("FAST_LINUX_I",arch,name,after["status"],delta,
              "undeclared="+after["undeclared"],after["diagnostic"])
    print("FAST_LINUX_I_SUMMARY",json.dumps(dict(owners=len(results),
          regressed=regressed, source_to_s_only=True), sort_keys=True))
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(dict(schema="exact-gnu-i-quick-ab-v1",
            verdict="NO_GNU_AS_LINK_QEMU",results=results), indent=2)+"\n")
    return 1 if regressed else 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as exc:
        print("FAST_LINUX_I_INFRA_FAIL",exc,file=sys.stderr)
        sys.exit(2)
