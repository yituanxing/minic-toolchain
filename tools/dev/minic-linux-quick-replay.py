#!/usr/bin/env python3
"""Fast frozen-GNU Linux .i A/B replay. NOT full LD/QEMU certification.

Use --corpus ARCH=gnu-i.zip, --owner ARCH:path.o, --minic new/minic-cc
and optionally --before old/minic-cc. Only selected SHA-checked .i files
are extracted; both compilers receive identical original Kbuild ABI flags.
Use --roots three-linux-all-root-causes.json --top 20 to select one owner
per top diagnostic class, avoiding a 21-shard rescan for each code fix.
Optional --gnu-as checks each resulting object with original GNU machine
assembler flags and nm -u; unresolved symbols are diagnostics, NOT a link.
"""
import argparse
import concurrent.futures as futures
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import zipfile

ARCHES = ("x86_64", "aarch64", "loongarch64")
GNU_CC = {"x86_64": "gcc", "aarch64": "aarch64-linux-gnu-gcc",
          "loongarch64": "loongarch64-linux-gnu-gcc-13"}
GNU_NM = {"x86_64": "nm", "aarch64": "aarch64-linux-gnu-nm",
          "loongarch64": "loongarch64-linux-gnu-nm"}
ERR = re.compile(r"[^ :]+\.(?:i|c|h|s):\d+(?::\d+)?:")
NAME = re.compile(r"FRONTEND_LOOKUP_DETAIL name=([^ ]+)")
ASSERT = re.compile(r"__compiletime_assert_[0-9]+$")

def frozen_oracle():
    """Share the single GNU Kbuild target-option authority with CI."""
    path = Path(__file__).resolve().parents[1] / "ci/linux-three-isa-freeze-i-v1.py"
    spec = importlib.util.spec_from_file_location("minic_quick_frozen_oracle", path)
    if spec is None or spec.loader is None:
        raise ValueError("cannot load canonical GNU Kbuild option validator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def sha(data):
    return hashlib.sha256(data).hexdigest()

def load(archive, arch, owners, samples, oracle=None, assemble=False):
    oracle = oracle if oracle is not None else frozen_oracle()
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
            if not oracle.valid_minic_flags(flags, arch):
                raise ValueError("invalid original Kbuild MiniC flags")
            asm_flags = row.get("gnu_as_flags")
            if assemble and not oracle.valid_assembler_flags(asm_flags):
                raise ValueError("invalid original GNU assembler flags " + name)
            content = z.read(member)
            if sha(content) != row["sha256"]:
                raise ValueError("original GNU input SHA mismatch " + name)
            tasks.append((arch, name, row["index"], flags, asm_flags, content))
        return tasks

def run(task, tag, compiler, tempdir, timeout, gnu_cc=None, gnu_nm=None,
        assembler_cwd=None):
    arch, name, index, flags, asm_flags, content = task
    started = time.monotonic()
    def timed(result):
        result["elapsed_ms"] = round((time.monotonic() - started) * 1000, 1)
        return result
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
        result = dict(status="S_PASS" if good else "S_FAIL",
                      diagnostic=ERR.sub("<source>:", errors[0])[:220] if errors else "",
                      undeclared=refs[-1] if refs else "",
                      asm_sha256=sha(asm.read_bytes()) if good else None)
        if not good or gnu_cc is None:
            return timed(result)
        obj = d / "source.o"
        ass = subprocess.run([gnu_cc, *asm_flags, "-x", "assembler", "-c",
                              str(asm), "-o", str(obj)],
                             cwd=assembler_cwd, capture_output=True, timeout=timeout)
        if ass.returncode != 0 or not obj.is_file() or obj.stat().st_size == 0:
            result.update(status="GNU_AS_FAIL",
                          diagnostic=ass.stderr.decode(errors="replace")[-240:],
                          undefined_symbols=None, assert_symbols=None)
            return timed(result)
        undef = subprocess.run([gnu_nm, "-u", str(obj)],
                               capture_output=True, timeout=timeout)
        if undef.returncode != 0:
            result.update(status="GNU_NM_FAIL",
                          diagnostic=undef.stderr.decode(errors="replace")[-240:],
                          undefined_symbols=None, assert_symbols=None)
            return timed(result)
        symbols = sorted({parts[-1] for line in
                          undef.stdout.decode(errors="replace").splitlines()
                          if len(parts := line.split()) == 2 and
                          parts[0] in ("U", "w", "v", "W", "V")})
        result.update(status="S_GNU_AS_PASS", obj_sha256=sha(obj.read_bytes()),
                      undefined_symbols=symbols,
                      assert_symbols=[symbol for symbol in symbols
                                      if ASSERT.fullmatch(symbol)])
        return timed(result)
    except subprocess.TimeoutExpired:
        return timed(dict(status="S_TIMEOUT", diagnostic="timeout", undeclared="",
                          asm_sha256=None))

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--corpus", action="append", default=[], help="ARCH=gnu-i.zip")
    p.add_argument("--owner", action="append", default=[], help="ARCH:owner.o")
    p.add_argument("--roots", type=Path, help="attested 21-shard root-cause JSON")
    p.add_argument("--top", type=int, default=20)
    p.add_argument("--sample", type=int, default=0)
    p.add_argument("--minic", type=Path, required=True)
    p.add_argument("--before", type=Path)
    p.add_argument("--gnu-as", action="store_true",
                   help="also assemble with original GNU Kbuild flags, then inspect nm -u; NOT full LD/QEMU")
    p.add_argument("--gnu-cc", action="append", default=[], metavar="ARCH=GNU_CC",
                   help="override the GNU assembler-driver per target")
    p.add_argument("--gnu-nm", action="append", default=[], metavar="ARCH=GNU_NM",
                   help="override the target nm per target")
    p.add_argument("--gnu-as-cwd", type=Path,
                   help="original GNU build directory for relative .incbin files")
    p.add_argument("--workers", type=int, default=4)
    p.add_argument("--timeout", type=int, default=80)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    if not 1 <= args.workers <= 32 or not 1 <= args.timeout <= 3600:
        p.error("invalid worker count or timeout")
    if not args.gnu_as and (args.gnu_cc or args.gnu_nm or args.gnu_as_cwd):
        p.error("GNU assembler options require --gnu-as")
    gnu_cc = dict(GNU_CC)
    gnu_nm = dict(GNU_NM)
    for choices, destination in ((args.gnu_cc, gnu_cc), (args.gnu_nm, gnu_nm)):
        seen = set()
        for item in choices:
            arch, sep, executable = item.partition("=")
            if (not sep or arch not in ARCHES or arch in seen or
                    not executable or executable.strip() != executable):
                p.error("invalid or duplicate ARCH=executable")
            seen.add(arch)
            destination[arch] = executable
    asm_cwd = args.gnu_as_cwd.resolve(strict=True) if args.gnu_as_cwd else None
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
    oracle = frozen_oracle()
    for arch, archive in archives.items():
        if selected[arch] or args.sample:
            tasks.extend(load(archive, arch, selected[arch], args.sample,
                              oracle=oracle, assemble=args.gnu_as))
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
                         run(x[0], x[1], x[2], temp, args.timeout,
                             gnu_cc[x[0][0]] if args.gnu_as else None,
                             gnu_nm[x[0][0]] if args.gnu_as else None,
                             asm_cwd)), work))
    got = {(a,b,label):r for a,b,label,r in receipts}
    results = []
    regressed = 0
    pass_status = "S_GNU_AS_PASS" if args.gnu_as else "S_PASS"
    for arch, name, index, flags, asm_flags, content in tasks:
        after = got[arch,name,"after"]
        before = got.get((arch,name,"before"))
        if before and before["status"] == pass_status and after["status"] != pass_status:
            delta = "REGRESSED"
            regressed += 1
        elif before and before["status"] != pass_status and after["status"] == pass_status:
            delta = "IMPROVED"
        elif (before and args.gnu_as and before["status"] == pass_status and
              after["status"] == pass_status and
              set(after["assert_symbols"]) - set(before["assert_symbols"])):
            delta = "REGRESSED"
            regressed += 1
        elif (before and args.gnu_as and before["status"] == pass_status and
              after["status"] == pass_status and
              set(before["assert_symbols"]) - set(after["assert_symbols"])):
            delta = "IMPROVED"
        elif (before and before["status"] != pass_status and
              after["status"] != pass_status and
              (before["diagnostic"], before["undeclared"]) !=
              (after["diagnostic"], after["undeclared"])):
            delta = "FRONTIER_MOVED"
        else:
            delta = "UNCHANGED" if before else "OBSERVED"
        results.append(dict(target=arch, owner=name, source_sha256=sha(content),
                            flags=flags, gnu_as_flags=asm_flags if args.gnu_as else None,
                            after=after, before=before, delta=delta))
        print("FAST_LINUX_I",arch,name,after["status"],delta,
              "undeclared="+after["undeclared"],
              "asserts="+",".join(after.get("assert_symbols") or []),
              after["diagnostic"])
    print("FAST_LINUX_I_SUMMARY",json.dumps(dict(owners=len(results),
          regressed=regressed, source_to_s_only=not args.gnu_as,
          original_gnu_as_nm_diagnostic=args.gnu_as,
          full_gnu_ld_qemu_certified=False), sort_keys=True))
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(dict(schema="exact-gnu-i-quick-ab-v1",
            verdict="NO_FULL_GNU_LD_QEMU" if args.gnu_as else "NO_GNU_AS_LINK_QEMU",
            results=results), indent=2)+"\n")
    return 1 if regressed else 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as exc:
        print("FAST_LINUX_I_INFRA_FAIL",exc,file=sys.stderr)
        sys.exit(2)
