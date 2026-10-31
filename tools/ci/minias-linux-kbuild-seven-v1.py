#!/usr/bin/env python3
"""Freeze *actual* Linux Kbuild -save-temps=obj assembly in seven SHA-bound shards.

The native Kbuild .config, command (.cmd), object and produced .s belong to
the SAME build, not an unrelated C test or a different architecture/config.
A producer that fails Kbuild still exports an explicitly INCOMPLETE corpus;
it can discover syntax, but cannot certify the complete Linux input universe.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import io
import json
import pathlib
import re
import resource
import struct
import subprocess
import sys
import tarfile

ARCH = {"x86_64": (62, "x86_64"), "aarch64": (183, "arm64"),
        "loongarch64": (258, "loongarch")}
LINUX_ARCHIVE_SHA256 = "dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932"
SHARDS = 7
# This particular immutable Linux 6.6.143 GNU Kbuild corpus is a separate
# acceptance contract from the general discovery/inventory mode.
STRICT_KBUILD_TOTAL = {"x86_64": 2763, "aarch64": 4140,
                       "loongarch64": 2742}
# Original Linux .S uses real producer-created files, not placeholders.
# Freeze their bytes with the same GNU Kbuild provenance as the .s/.cmd.
INCBIN_LINE = re.compile(rb'(?m)^[ \t]*\.incbin[ \t]+"([^"\r\n]+)"')


def original_incbin_inputs(asm: pathlib.Path, out: pathlib.Path) -> list[dict]:
    deps = []
    for raw in sorted(set(INCBIN_LINE.findall(asm.read_bytes()))):
        try:
            name = raw.decode("utf-8", errors="strict")
        except UnicodeDecodeError as exc:
            raise ValueError("non-UTF8 .incbin GNU input") from exc
        rel = pathlib.PurePosixPath(name)
        if rel.is_absolute() or ".." in rel.parts or not rel.parts:
            raise ValueError(f"unsafe original Kbuild .incbin path: {name}")
        produced = out / pathlib.Path(*rel.parts)
        if not produced.is_file():
            # Kbuild also permits source-root relative data; only use the
            # original published Linux source tree, never dummy bytes.
            produced = out.parent / "linux-6.6.143" / pathlib.Path(*rel.parts)
        if not produced.is_file():
            raise ValueError(f"authentic original Kbuild .incbin missing: {name}")
        if produced.stat().st_size > 128 * 1024 * 1024:
            raise ValueError(f"Kbuild .incbin unexpectedly large: {name}")
        deps.append({"path": name, "sha256": sha(produced),
                     "size": produced.stat().st_size,
                     "origin": "build" if (out / pathlib.Path(*rel.parts)).is_file()
                               else "source"})
    return deps


def sha(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def is_target_object(path: pathlib.Path, machine: int) -> bool:
    with path.open("rb") as f:
        header = f.read(20)
    return (len(header) == 20 and header[:6] == b"\x7fELF\x02\x01" and
            struct.unpack_from("<HH", header, 16) == (1, machine))


def kind_of_command(first: str) -> str:
    # Kbuild's first command is the only authority: never count dependencies
    # listed below it as C input or claim that a host object was target C.
    matches = re.findall(r"(?:^|\s)(?:\S*/)?\S+\.(c|S)(?=\s|$)", first)
    return {"c": "C", "S": "native_S"}.get(matches[-1], "OTHER") if matches else "OTHER"


def export(a: argparse.Namespace) -> None:
    out = pathlib.Path(a.out).resolve()
    dest = pathlib.Path(a.dest).resolve()
    config = out / ".config"
    if not config.is_file():
        raise ValueError("real Kbuild .config missing")
    if dest.exists() and any(dest.iterdir()):
        raise ValueError("refusing to overwrite frozen corpus")
    machine, karch = ARCH[a.target]
    records = []
    mismatches = Counter()
    all_objects = sorted(out.rglob("*.o"))
    for obj in all_objects:
        if not is_target_object(obj, machine):
            continue
        rel = obj.relative_to(out)
        if rel.parts[0] in ("scripts", "tools"):
            # Host-built scripts can be ELF64 x86_64, but aren't target TUs.
            continue
        cmd = obj.with_name("." + obj.name + ".cmd")
        if not cmd.is_file():
            mismatches["target_o_without_cmd"] += 1
            continue
        cmd_text = cmd.read_text(errors="replace")
        first = cmd_text.split("\nsource_", 1)[0].split("\ndeps_", 1)[0]
        if "-D__KERNEL__" not in first:
            mismatches["not_KBUILD_target_command"] += 1
            continue
        asm = obj.with_suffix(".s")
        if not asm.is_file() or not asm.stat().st_size:
            mismatches["target_o_without_saved_s"] += 1
            continue
        kind = kind_of_command(first)
        if kind == "OTHER":
            mismatches["unrecognized_target_command"] += 1
        rel_s = asm.relative_to(out).as_posix()
        rel_cmd = cmd.relative_to(out).as_posix()
        record = {
            "owner": rel.as_posix(),
            "s": rel_s, "cmd": rel_cmd,
            "kind": kind, "s_sha256": sha(asm),
            "cmd_sha256": sha(cmd), "gnu_object_sha256": sha(obj)
        }
        if a.include_incbin:
            record["incbins"] = original_incbin_inputs(asm, out)
        records.append(record)
    if not records:
        raise ValueError("no target-linked Kbuild C/.S assembly pairs")
    if len({r["owner"] for r in records}) != len(records):
        raise ValueError("duplicate .o owner")
    cfg = sha(config)
    machine_name = subprocess.run(
        [a.compiler, "-dumpmachine"], capture_output=True, text=True, check=True
    ).stdout.strip()
    compiler_version = subprocess.run(
        [a.compiler, "-dumpfullversion"], capture_output=True, text=True, check=True
    ).stdout.strip()
    image = out / "vmlinux"
    complete = a.build_exit == 0 and image.is_file() and image.stat().st_size != 0
    identity = {
        "schema": "minias-linux-kbuild-s-seven-v1",
        "authentic_incbin_frozen": a.include_incbin,
        "linux_version": "6.6.143",
        "linux_archive_sha256": LINUX_ARCHIVE_SHA256,
        "target": a.target, "kbuild_arch": karch,
        "config_sha256": cfg, "compiler_command": a.compiler,
        "compiler_triple": machine_name, "compiler_version": compiler_version,
        "compiler_sha256": sha(pathlib.Path(a.compiler_path).resolve()),
        "compiler_executable": str(pathlib.Path(a.compiler_path).resolve()),
        "kcflags": "-save-temps=obj", "kaflags": "-save-temps=obj",
        "build_goal": "vmlinux", "build_exit": a.build_exit,
        "full_kbuild": complete, "vmlinux_sha256": sha(image) if complete else None,
        "unpaired": dict(mismatches),
        "total": len(records),
        "kind_counts": dict(Counter(r["kind"] for r in records)),
        "sources": records,
    }
    payload = (json.dumps(identity, sort_keys=True, indent=2) + "\n").encode()
    dest.mkdir(parents=True, exist_ok=True)
    (dest / "manifest.json").write_bytes(payload)
    (dest / "config.sha256").write_text(cfg + "\n")
    for shard in range(SHARDS):
        members = [r for i, r in enumerate(records) if i % SHARDS == shard]
        if not members:
            raise ValueError("too few real objects for seven shards")
        with tarfile.open(dest / f"shard-{shard}.tar.gz", "w:gz",
                          compresslevel=5) as tar:
            pinned_assets = {}
            for r in members:
                for dep in r.get("incbins", []):
                    previous = pinned_assets.setdefault(dep["path"], dep)
                    if previous != dep:
                        raise ValueError("inconsistent original .incbin provenance")
            m = tarfile.TarInfo("manifest.json")
            m.size = len(payload)
            m.mode = 0o644
            tar.addfile(m, io.BytesIO(payload))
            for r in members:
                for attr, prefix in (("s", "asm"), ("cmd", "cmd")):
                    rel = pathlib.PurePosixPath(r[attr])
                    if rel.is_absolute() or ".." in rel.parts:
                        raise ValueError("path escape in Kbuild manifest")
                    tar.add(out / pathlib.Path(*rel.parts),
                            arcname=f"{prefix}/{rel.as_posix()}", recursive=False)
            for asset in pinned_assets.values():
                rel = pathlib.PurePosixPath(asset["path"])
                root = out if asset["origin"] == "build" else out.parent / "linux-6.6.143"
                real = root / pathlib.Path(*rel.parts)
                if sha(real) != asset["sha256"]:
                    raise ValueError("generated Kbuild .incbin changed while freezing")
                tar.add(real, arcname="incbin/" + asset["path"],
                        recursive=False)
    print(f"MINIAS_LINUX_S_FREEZE target={a.target} total={len(records)} "
          f"C={identity['kind_counts'].get('C', 0)} "
          f"native_S={identity['kind_counts'].get('native_S', 0)} "
          f"other={identity['kind_counts'].get('OTHER', 0)} "
          f"config={cfg} full_kbuild={complete} "
          f"build_exit={a.build_exit} unpaired={dict(mismatches)}", flush=True)


def safe_tar_members(tar: tarfile.TarFile) -> dict[str, tarfile.TarInfo]:
    members = {}
    for member in tar:
        p = pathlib.PurePosixPath(member.name)
        if not member.isfile() or p.is_absolute() or ".." in p.parts:
            raise ValueError("unsafe tar member")
        if member.name in members:
            raise ValueError("duplicate tar member")
        members[member.name] = member
    return members


def checked_shard(path: pathlib.Path, target: str, shard: int):
    with tarfile.open(path) as tar:
        members = safe_tar_members(tar)
        if "manifest.json" not in members:
            raise ValueError("missing frozen manifest")
        manifest_bytes = tar.extractfile(members["manifest.json"]).read()
        manifest = json.loads(manifest_bytes)
        if manifest.get("schema") != "minias-linux-kbuild-s-seven-v1":
            raise ValueError("wrong corpus schema")
        if manifest.get("target") != target:
            raise ValueError("wrong target identity")
        if manifest.get("linux_archive_sha256") != LINUX_ARCHIVE_SHA256:
            raise ValueError("Linux tarball provenance mismatch")
        src = manifest["sources"]
        if len(src) != manifest["total"] or len({r["owner"] for r in src}) != len(src):
            raise ValueError("not a complete Kbuild owner inventory")
        selected = [r for i, r in enumerate(src) if i % SHARDS == shard]
        expected = {"manifest.json"}
        for r in selected:
            expected |= {"asm/" + r["s"], "cmd/" + r["cmd"]}
            for dep in r.get("incbins", []):
                rel = pathlib.PurePosixPath(dep["path"])
                if rel.is_absolute() or ".." in rel.parts:
                    raise ValueError("unsafe frozen .incbin pathname")
                expected.add("incbin/" + dep["path"])
        if set(members) != expected:
            raise ValueError("incomplete/extra frozen shard files")
        for r in selected:
            for prefix, field, hash_field in [
                ("asm/", "s", "s_sha256"), ("cmd/", "cmd", "cmd_sha256")
            ]:
                value = tar.extractfile(members[prefix + r[field]]).read()
                if hashlib.sha256(value).hexdigest() != r[hash_field]:
                    raise ValueError("frozen Kbuild input digest mismatch")
        for r in selected:
            for dep in r.get("incbins", []):
                value = tar.extractfile(members["incbin/" + dep["path"]]).read()
                if len(value) != dep["size"] or hashlib.sha256(value).hexdigest() != dep["sha256"]:
                    raise ValueError("authentic Kbuild .incbin hash/size mismatch")
        return manifest, hashlib.sha256(manifest_bytes).hexdigest(), selected


def assembly_census(text: str) -> tuple[Counter, Counter]:
    """Read GNU -S/.S surface without asking any target backend to parse it.

    Lexical inventory only: actual Target parser + GNU semantic oracle remain
    mandatory. Counts are per architecture, not a bogus cross-ISA opcode pool.
    """
    instructions = Counter()
    directives = Counter()
    for line in text.splitlines():
        code = line.strip()
        if not code or code.startswith(("#", "//", "/*", "*", ";")):
            continue
        if code.startswith("."):
            name = code.split(None, 1)[0]
            # Labels such as .Lxx: are not directives.
            if not name.endswith(":") and re.fullmatch(r"\.[a-z][a-z0-9_]*", name):
                directives[name] += 1
            continue
        token = code.split(None, 1)[0]
        if token.endswith(":"):
            continue
        if re.fullmatch(r"[a-zA-Z][a-zA-Z0-9._]*", token):
            instructions[token.lower()] += 1
    return instructions, directives


def bound_minias_address_space() -> None:
    # Hard cap EACH assembler subprocess (not the GNU producer nor Python).
    # Prevent a pathological .s from tearing down the entire Linux runner.
    budget = 2 * 1024 * 1024 * 1024
    resource.setrlimit(resource.RLIMIT_AS, (budget, budget))


def first_blocker_family(reason: str) -> str:
    """Group identical target ISA deficits regardless of operand spellings.

    x86/Loong source strings can contain symbol names, index registers and
    offsets; counting those as separate root causes obscures a real
    architecture-level issue. Preserve full diagnostics in each record.
    """
    m = re.match(
        r"^(unsupported-x86_64-operand|unsupported-loongarch64-operands|"
        r"bad-aarch64-operands|unsupported-directive):([^:\s]+)", reason)
    if m:
        return m.group(1) + ":" + m.group(2)
    return reason.split(":line=", 1)[0][:120]


def first_blocker_context(assembly: bytes, diagnostic: str) -> tuple[int | None, list[str]]:
    """Only preserve bounded ORIGINAL GNU source near the first error.

    Every corpus owner and command are SHA-pinned, and this diagnostic
    never reclassifies a failing object as assembled or GNU-parity proven.
    """
    match = re.search(r":line=(\d+)", diagnostic)
    if match is None:
        return None, []
    line = int(match.group(1))
    source = assembly.decode("utf-8", errors="replace").splitlines()
    if line < 1 or line > len(source):
        return line, []
    lo, hi = max(1, line - 2), min(len(source), line + 2)
    return line, [f"{i}:{source[i - 1].strip()[:180]}" for i in range(lo, hi + 1)]


def scan(a: argparse.Namespace) -> None:
    p = pathlib.Path(a.archive)
    manifest, manifest_sha, selected = checked_shard(p, a.target, a.shard)
    destination = pathlib.Path(a.dest).resolve()
    if destination.exists() and any(destination.iterdir()):
        raise ValueError("refusing previous scan results")
    destination.mkdir(parents=True)
    rows = []
    instructions = Counter()
    directives = Counter()
    # Download/extract only one seven-way shard. Kbuild and GCC are NOT rerun.
    with tarfile.open(p) as tar:
        members = safe_tar_members(tar)
        # Input blobs are original Kbuild producer outputs. Extract once
        # under a per-shard isolated assembly CWD, never into the repo.
        for r in selected:
            for dep in r.get("incbins", []):
                asset = destination / "inputs" / dep["path"]
                if not asset.exists():
                    asset.parent.mkdir(parents=True, exist_ok=True)
                    asset.write_bytes(tar.extractfile(
                        members["incbin/" + dep["path"]]).read())
        for index, r in enumerate(manifest["sources"]):
            if index % SHARDS != a.shard: continue
            inp = destination / "inputs" / r["s"]
            inp.parent.mkdir(parents=True, exist_ok=True)
            assembly_bytes = tar.extractfile(members["asm/" + r["s"]]).read()
            inp.write_bytes(assembly_bytes)
            inst, dirs = assembly_census(assembly_bytes.decode("utf-8", errors="replace"))
            instructions.update(inst)
            directives.update(dirs)
            output = destination / "objects" / r["owner"]
            output.parent.mkdir(parents=True, exist_ok=True)
            if len(rows) % 25 == 0:
                print(f"MINIAS_LINUX_S_PROGRESS target={a.target} "
                      f"shard={a.shard} owner={r['owner']} "
                      f"done={len(rows)}/{len(selected)}", flush=True)
            try:
                run = subprocess.run(
                    [a.minias, "--target=" + a.target, "-o", str(output), str(inp)],
                    capture_output=True, text=True, timeout=45,
                    cwd=destination / "inputs",
                    preexec_fn=bound_minias_address_space
                )
                rc, diag = run.returncode, (run.stderr or run.stdout).strip()
            except subprocess.TimeoutExpired:
                rc, diag = 124, "process-timeout"
            except OSError as e:
                rc, diag = 127, "process-start-error:" + str(e)
            if rc == 0 and output.is_file() and output.stat().st_size:
                if is_target_object(output, ARCH[a.target][0]):
                    state, reason = "ASSEMBLED_UNVERIFIED", "-"
                else:
                    state, reason = "INFRA_ERROR", "success-wrong-ELF-target"
            elif rc == 0:
                state, reason = "INFRA_ERROR", "success-no-object"
            else:
                state = "CANDIDATE_FAIL"
                first = (diag.splitlines() or ["no-diagnostic"])[0]
                first = re.sub(r":line=\d+", ":line=N", first)
                reason = first.replace("minic-as: ", "")[:180]
            blocker_line, source_excerpt = (
                first_blocker_context(assembly_bytes, diag)
                if state == "CANDIDATE_FAIL" else (None, []))
            rows.append({"index": index, "owner": r["owner"], "status": state,
                         "first_blocker": reason,
                         "first_blocker_family": (first_blocker_family(reason)
                                                  if state == "CANDIDATE_FAIL"
                                                  else "-"),
                         "blocker_line": blocker_line,
                         "source_excerpt": source_excerpt,
                         "exit_code": rc,
                         "diagnostic": diag[:2000]})
    status = Counter(r["status"] for r in rows)
    result = {
        "schema": "minias-kbuild-shard-result-v1", "target": a.target,
        "shard": a.shard, "manifest_sha256": manifest_sha,
        "config_sha256": manifest["config_sha256"],
        "full_kbuild": manifest["full_kbuild"],
        "expected_total": manifest["total"], "records": rows,
        "mnemonics": dict(instructions), "directives": dict(directives)
    }
    (destination / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"MINIAS_LINUX_S_SHARD target={a.target} shard={a.shard} "
          f"scanned={len(rows)} assembled_unverified={status['ASSEMBLED_UNVERIFIED']} "
          f"candidate_fail={status['CANDIDATE_FAIL']} "
          f"infra_error={status['INFRA_ERROR']}", flush=True)
    if status["INFRA_ERROR"]:
        raise ValueError("cannot accept false-success assembler outputs")
    if a.strict and (status["CANDIDATE_FAIL"] or
                     status["ASSEMBLED_UNVERIFIED"] != len(selected) or
                     not manifest["full_kbuild"] or
                     manifest["total"] != STRICT_KBUILD_TOTAL[a.target]):
        raise ValueError("strict Linux Kbuild shard failed: " + a.target +
                         f" shard={a.shard} candidate_fail={status['CANDIDATE_FAIL']}" +
                         f" full_kbuild={manifest['full_kbuild']}")


def aggregate(a: argparse.Namespace) -> None:
    paths = sorted(pathlib.Path(a.results).rglob("results.json"))
    selected = tuple(a.targets.split(",")) if a.targets else tuple(ARCH)
    if not selected or len(set(selected)) != len(selected) or any(
            target not in ARCH for target in selected):
        raise ValueError("invalid aggregate architecture selection")
    expected = len(selected) * SHARDS
    if len(paths) != expected:
        raise ValueError(f"missing/duplicate result sets {len(paths)}/{expected}")
    data = {t: {} for t in selected}
    for p in paths:
        doc = json.loads(p.read_text())
        t, shard = doc.get("target"), doc.get("shard")
        if doc.get("schema") != "minias-kbuild-shard-result-v1":
            raise ValueError("wrong result schema")
        if t not in data or not isinstance(shard, int) or not 0 <= shard < SHARDS:
            raise ValueError("invalid result target/shard")
        if shard in data[t]: raise ValueError("duplicated result shard")
        data[t][shard] = doc
    summary = {}
    gate_failures = []
    for target, seven in data.items():
        if set(seven) != set(range(SHARDS)):
            raise ValueError("incomplete target seven-shard set")
        ids = {doc["manifest_sha256"] for doc in seven.values()}
        cfgs = {doc["config_sha256"] for doc in seven.values()}
        totals = {doc["expected_total"] for doc in seven.values()}
        fulls = {doc["full_kbuild"] for doc in seven.values()}
        if any(len(v) != 1 for v in (ids, cfgs, totals, fulls)):
            raise ValueError("mixed Kbuild/config/corpus identity")
        total = totals.pop()
        rows = [r for i in range(SHARDS) for r in seven[i]["records"]]
        if sorted(r["index"] for r in rows) != list(range(total)):
            raise ValueError("incomplete target owner union")
        if len({r["owner"] for r in rows}) != total:
            raise ValueError("duplicated target owner")
        if any(r["index"] % SHARDS != n for n in range(SHARDS)
               for r in seven[n]["records"]):
            raise ValueError("wrong shard owner membership")
        statuses = Counter(r["status"] for r in rows)
        mnemonic_counts = Counter()
        directive_counts = Counter()
        for d in seven.values():
            mnemonic_counts.update(d.get("mnemonics", {}))
            directive_counts.update(d.get("directives", {}))
        causes = Counter(r["first_blocker"] for r in rows
                         if r["status"] == "CANDIDATE_FAIL")
        families = Counter(r.get("first_blocker_family",
                                  first_blocker_family(r["first_blocker"]))
                           for r in rows if r["status"] == "CANDIDATE_FAIL")
        if statuses["INFRA_ERROR"]:
            raise ValueError("infrastructure failure: " + target)
        complete = (fulls == {True} and total == STRICT_KBUILD_TOTAL[target] and
                    statuses == {"ASSEMBLED_UNVERIFIED": total})
        if a.strict and not complete:
            gate_failures.append(f"{target}: total={total} expected={STRICT_KBUILD_TOTAL[target]} "
                                 f"full_kbuild={fulls} status={dict(statuses)}")
        summary[target] = {"target": target, "config_sha256": cfgs.pop(),
                           "manifest_sha256": ids.pop(),
                           "full_kbuild": fulls.pop(), "total": total,
                           "strict_candidate_pass": complete,
                           "status": dict(statuses),
                           "first_blockers": dict(causes.most_common()),
                           "first_blocker_families": dict(families.most_common()),
                           "mnemonics": dict(mnemonic_counts.most_common()),
                           "directives": dict(directive_counts.most_common())}
        print(f"MINIAS_LINUX_S_ARCH target={target} total={total} "
              f"assembled_unverified={statuses['ASSEMBLED_UNVERIFIED']} "
              f"candidate_fail={statuses['CANDIDATE_FAIL']} "
              f"full_kbuild={summary[target]['full_kbuild']}")
        for cause, count in causes.most_common(15):
            print(f"MINIAS_LINUX_S_BLOCKER target={target} n={count} cause={cause}")
        for family, count in families.most_common(30):
            print(f"MINIAS_LINUX_S_FAMILY target={target} n={count} cause={family}")
        for record in rows:
            if record["status"] != "CANDIDATE_FAIL":
                continue
            print(f"MINIAS_LINUX_S_OWNER target={target} "
                  f"owner={record['owner']} line={record.get('blocker_line')} "
                  f"root={record['first_blocker']}")
            for source_line in record.get("source_excerpt", []):
                print(f"MINIAS_LINUX_S_CONTEXT target={target} "
                      f"owner={record['owner']} text={source_line}")
        for name, count in mnemonic_counts.most_common(30):
            print(f"MINIAS_LINUX_S_ISA target={target} mnemonic={name} uses={count}")
        for name, count in directive_counts.most_common(15):
            print(f"MINIAS_LINUX_S_DIRECTIVE target={target} directive={name} uses={count}")
    dest = pathlib.Path(a.dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps({"schema": "minias-linux-3x7-kbuild-v1",
                                "targets": summary}, sort_keys=True, indent=2) + "\n")
    print(f"MINIAS_LINUX_S_DISCOVERY=COMPLETE targets={len(selected)} "
          f"shards={expected} GNU_ORACLE=NOT_TESTED P1=NOT_TESTED",
          flush=True)
    if a.strict:
        if gate_failures:
            raise ValueError("strict Linux Kbuild candidate gate failed: " +
                             "; ".join(gate_failures))
        print(f"MINIAS_LINUX_S_STRICT_GATE=PASS targets={len(selected)} "
              f"shards={expected} candidates={sum(x['total'] for x in summary.values())} "
              "GNU_ORACLE=NOT_TESTED P1=NOT_TESTED", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    modes = parser.add_subparsers(dest="action", required=True)
    exp = modes.add_parser("export")
    exp.add_argument("--target", choices=ARCH, required=True)
    exp.add_argument("--out", required=True)
    exp.add_argument("--dest", required=True)
    exp.add_argument("--compiler", required=True)
    exp.add_argument("--compiler-path", required=True)
    exp.add_argument("--build-exit", type=int, required=True)
    exp.add_argument("--include-incbin", action="store_true",
                     help="freeze authentic GNU Kbuild generated .incbin files")
    run = modes.add_parser("scan")
    run.add_argument("--target", choices=ARCH, required=True)
    run.add_argument("--shard", type=int, choices=range(SHARDS), required=True)
    run.add_argument("--archive", required=True)
    run.add_argument("--minias", required=True)
    run.add_argument("--dest", required=True)
    run.add_argument("--strict", action="store_true",
                     help="fail on any candidate error or incomplete pinned Kbuild")
    agg = modes.add_parser("aggregate")
    agg.add_argument("--results", required=True)
    agg.add_argument("--targets",
                     help="Comma-separated target selection; defaults to all three")
    agg.add_argument("--dest", required=True)
    agg.add_argument("--strict", action="store_true",
                     help="require every original Linux 6.6.143 owner to assemble")
    a = parser.parse_args()
    try:
        {"export": export, "scan": scan, "aggregate": aggregate}[a.action](a)
        return 0
    except (ValueError, KeyError, OSError, tarfile.TarError, subprocess.CalledProcessError) as e:
        print(f"MINIAS_LINUX_S_INFRA_ERROR={e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
