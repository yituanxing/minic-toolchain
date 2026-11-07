#!/usr/bin/env python3
"""RV32 seven-shard partial MiniC/GNU-AS resume, using the 3-ISA checkpoint API.

One original GNU Kbuild C owner is a checkpoint transaction: the saved .o
and .s are SHA-authenticated, followed by an atomic per-owner JSON commit.
Restoring any success requires the EXACT compiler source/profile, binary,
original GNU environment, complete generated headers, frozen .o.cmd, and
recomputed GNU-E bytes. A mismatched artifact is rejected, never promoted.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys


def module(name: str, file: str):
    path = Path(__file__).with_name(file)
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ValueError(f"missing shared seven-shard helper: {file}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# Reuse the existing three-ISA seven-runner's atomic JSON persistence, safe
# object paths and streaming SHA256. No second checkpoint serialization model.
seven = module("minic_seven_partial", "linux-three-isa-seven-runtime-v1.py")
regen = module("minic_kbuild_regen", "linux-runtime-kbuild-regeneration-guard-v1.py")
shards = module("minic_existing_shard", "linux-runtime-gcc-shards-v1.py")
SCHEMA = "rv32i-current-seven-partial-v1"
STATUS = "MINIC_S_GNU_AS_PASS"


def checked(path: Path) -> Path:
    if not path.is_file() or path.is_symlink() or not path.stat().st_size:
        raise ValueError(f"missing/empty/symlinked evidence: {path}")
    return path


def owner_paths(listing: Path) -> list[str]:
    names = [str(seven.safe_owner(name)) for name in seven_names(listing)]
    if not names or len(names) != len(set(names)):
        raise ValueError("empty or duplicate RV32 shard membership")
    return names


def seven_names(path: Path) -> list[str]:
    return [line for line in checked(path).read_text().splitlines()
            if line and not line.startswith("#")]


def sha(path: Path) -> str:
    return seven.file_sha(checked(path))


def gnu_binary_sha(name: Path) -> str:
    # Distro cross tools are often symlinks. Reuse the 3-ISA seven-runner's
    # trusted GNU executable resolver; checkpoint/source objects stay strict.
    return seven.file_sha(seven.trusted_gnu_driver_binary(name.name))


def source_from_command(cmd: Path, out: Path, original_tree: Path) -> str:
    text = checked(cmd).read_text(errors="replace")
    saved = [line.partition(":=")[2].strip() for line in text.splitlines()
             if re.match(r"^(?:savedcmd|cmd)_[^\n]+:=", line)]
    if len(saved) != 1:
        raise ValueError(f"ambiguous frozen original GNU .o.cmd: {cmd}")
    args = shlex.split(saved[0])
    if not args or Path(args[0]).name != "riscv64-linux-gnu-gcc" or "-c" not in args:
        raise ValueError(f"non-GNU C source command: {cmd}")
    sources = [arg for i, arg in enumerate(args)
               if arg.endswith(".c") and (not i or args[i-1] not in ("-include", "-imacros"))]
    if len(sources) != 1:
        raise ValueError(f"ambiguous pinned GNU C source: {cmd}")
    actual = (out / sources[0]).resolve(strict=True)
    # Most TUs live in the unpacked kernel; a small set of real Kbuild C
    # producers lives in O= (e.g. generated offset/version sources). Freeze
    # the genuine .cmd-selected source, not a guessed owner->.c spelling.
    if not (actual.is_relative_to(original_tree.resolve(strict=True)) or
            actual.is_relative_to(out.resolve(strict=True))):
        raise ValueError(f"GNU Kbuild source outside original source/O= trees: {actual}")
    if (out / sources[0]).is_symlink():
        raise ValueError(f"symlinked GNU Kbuild C source {sources[0]}")
    return sha(actual)


def source_profile(root: Path) -> str:
    helper = root / "tools/ci/linux-runtime-profile-cache-id-v1.py"
    return subprocess.check_output([sys.executable, str(checked(helper))],
                                   cwd=root, text=True, timeout=30).strip()


def compose_identity(root: Path, out: Path, prov: Path, selection: Path,
                     current_cc: Path, source: Path, gcc: Path, assembler: Path):
    selected = owner_paths(selection)
    all_c = seven_names(prov / "full-c-objects.txt")
    if len(all_c) != 1741 or len(set(all_c)) != 1741:
        raise ValueError("exact GNU original C universe must be 1741")
    if selected != all_c[int(os.environ.get("RV32I_SHARD", "0"))::7]:
        raise ValueError("noncanonical seven-way RV32 current C partition")
    baseline = (prov / "gcc-baseline.txt").read_text()
    expected = {line.split("=", 1)[0]: line.split("=", 1)[1] for line in baseline.splitlines() if "=" in line}
    if (expected.get("elf_class") != "ELF32" or expected.get("isa") != "rv32i" or
            expected.get("abi") != "ilp32" or expected.get("c_owner_count") != "1741" or
            sha(out / ".config") != expected.get("config_sha256") or
            sha(out / "arch/riscv/boot/Image") != expected.get("image_sha256") or
            gnu_binary_sha(gcc) != expected.get("gcc_binary_sha256") or
            gnu_binary_sha(assembler) != expected.get("gnu_as_binary_sha256")):
        raise ValueError("GNU original config/ISA/ABI/GCC/GAS epoch differs")
    rows = []
    for name in selected:
        p = Path(name)
        cmd = out / p.parent / ("." + p.name + ".cmd")
        rows.append(dict(object=name, cmd_sha256=sha(cmd),
                         source_sha256=source_from_command(cmd, out, source)))
    result = {
        "schema": SCHEMA, "target": "riscv32-linux", "isa": "rv32i", "abi": "ilp32",
        "shard": int(os.environ.get("RV32I_SHARD", "0")),
        "source_archive_sha256": expected.get("linux_archive_sha256"),
        "config_sha256": sha(out / ".config"),
        "golden_image_sha256": sha(out / "arch/riscv/boot/Image"),
        "golden_all_objects_sha256": sha(prov / "gcc-objects.sha256"),
        "full_c_owner_list_sha256": sha(prov / "full-c-objects.txt"),
        "shard_c_owner_list_sha256": sha(selection),
        "current_compiler_source_profile_sha256": source_profile(root),
        "current_minic_compiler_sha256": sha(current_cc),
        "gnu_gcc_binary_sha256": gnu_binary_sha(gcc),
        "gnu_as_binary_sha256": gnu_binary_sha(assembler),
        "stage2_wrapper_sha256": sha(root / "tests/external/linux/stage2_kbuild_cc.sh"),
        "stage2_route_guard_sha256": sha(root / "tools/ci/linux-runtime-minic-route-audit-v1.py"),
        "generated_headers": regen.generated(out),
        "rows": rows,
    }
    if result["source_archive_sha256"] != "dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932":
        raise ValueError("original Linux 6.6.143 archive SHA differs")
    return result


def initialize(a):
    identity = compose_identity(a.root, a.out, a.provenance, a.selected,
                                a.minic, a.source, a.gnu_gcc, a.gnu_as)
    path = a.partial / "checkpoint/identity.json"
    if path.exists() and seven.json_file(path) != identity:
        raise ValueError("existing shard checkpoint identity mismatch")
    # The later selected-target Kbuild can rebuild the real RV32 vDSO and
    # change vdso-offsets.h. Keep the ORIGINAL GNU .cmd dependency graph before
    # any MiniC wrapper replaces .cmd metadata; never infer dependencies from
    # the later, mutable output tree.
    pinned = a.partial / "checkpoint/pinned-gnu-cmd"
    for row in identity["rows"]:
        owner = Path(row["object"])
        cmd = a.out / owner.parent / ("." + owner.name + ".cmd")
        if sha(cmd) != row["cmd_sha256"]:
            raise ValueError(f"original GNU .cmd drift while sealing {owner}")
        atomic_stage(cmd, pinned / owner.parent / ("." + owner.name + ".cmd"))
    atomic_stage(a.selected, a.partial / "checkpoint/frozen-selected.txt")
    seven.atomic_json(a.partial / "checkpoint/frozen-header-snapshot.json",
                      {"schema": regen.SCHEMA, "headers": identity["generated_headers"]})
    seven.atomic_json(path, identity)
    print(f"RV32I_SEVEN_PARTIAL_INIT=PASS shard={identity['shard']} "
          f"owners={len(identity['rows'])} current_compiler={identity['current_minic_compiler_sha256']}")


def identity(a):
    return seven.json_file(a.partial / "checkpoint/identity.json")


def pinned_gnu(a, idn: dict) -> tuple[Path, Path, Path]:
    checkpoint = a.partial / "checkpoint"
    pinned = checkpoint / "pinned-gnu-cmd"
    selected = checkpoint / "frozen-selected.txt"
    snapshot = checkpoint / "frozen-header-snapshot.json"
    if owner_paths(selected) != [r["object"] for r in idn["rows"]]:
        raise ValueError("frozen GNU C-owner membership differs from checkpoint")
    if seven.json_file(snapshot) != {
            "schema": regen.SCHEMA, "headers": idn["generated_headers"]}:
        raise ValueError("frozen GNU generated-header epoch is unauthenticated")
    for row in idn["rows"]:
        path = Path(row["object"])
        cmd = pinned / path.parent / ("." + path.name + ".cmd")
        if sha(cmd) != row["cmd_sha256"]:
            raise ValueError(f"frozen original GNU .cmd SHA differs: {path}")
    return pinned, selected, snapshot


def header_delta(a, idn: dict) -> tuple[dict, list[str], list[str]]:
    pinned, selected, snapshot = pinned_gnu(a, idn)
    affected_file = a.partial / "checkpoint/dynamic-header-affected.txt"
    changed, affected = regen.header_impact(a.out, snapshot, pinned,
                                            selected, affected_file)
    return regen.generated(a.out), changed, affected


def reconcile(a):
    idn = identity(a)
    if a.minic is None or sha(a.minic) != idn["current_minic_compiler_sha256"]:
        raise ValueError("current MiniC binary drift during header reconciliation")
    if a.root is None or source_profile(a.root) != idn["current_compiler_source_profile_sha256"]:
        raise ValueError("current MiniC source profile drift during header reconciliation")
    pinned, selected, _snapshot = pinned_gnu(a, idn)
    now, changed, affected = header_delta(a, idn)
    if affected:
        # An actual changed-header C dependency is never excused by a marker.
        # Replay ONLY the dependency-closed original GNU .cmd selected owners,
        # with the same current MiniC and GNU-AS route as the original sweep.
        # The stage2 wrapper atomically replaces each success checkpoint.
        env = {"MINIC_KEEP_INTERMEDIATES": "1",
               "MINIC_KBUILD_RV32I_ORIGIN_AUDIT": "1",
               "MINIC_KBUILD_RV32_PARTIAL_DIR": str(a.partial.resolve()),
               "MINIC_KBUILD_RV32_PARTIAL_OUT": str(a.out.resolve()),
               "MINIC_KBUILD_RV32_PARTIAL_HELPER": str(Path(__file__).resolve())}
        previous = {key: os.environ.get(key) for key in env}
        os.environ.update(env)
        affected_file = a.partial / "checkpoint/dynamic-header-affected.txt"
        try:
            regen.replay_affected(
                a.out, pinned, affected_file,
                a.root / "tests/external/linux/stage2_kbuild_cc.sh", a.minic,
                a.partial / "checkpoint/dynamic-header-success.txt",
                a.partial / "checkpoint/dynamic-header-hashes.sha256")
        finally:
            for key, old in previous.items():
                if old is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = old
    if regen.generated(a.out) != now:
        raise ValueError("generated GNU headers kept drifting during dynamic MiniC replay")
    seven.atomic_json(a.partial / "checkpoint/post-generated-header-epoch.json",
                      {"schema": SCHEMA, "headers": now,
                       "changed": changed, "recompiled": affected})
    print(f"RV32I_SEVEN_HEADER_RECONCILE=PASS changed={len(changed)} "
          f"dependency_closed_current_C={len(affected)} no_GNU_C_fallback=true")


def get_row(idn: dict, owner: str) -> dict:
    name = str(seven.safe_owner(owner))
    matches = [r for r in idn["rows"] if r["object"] == name]
    if len(matches) != 1:
        raise ValueError(f"unselected or duplicate RV32 current C owner: {name}")
    return matches[0]


def atomic_stage(source: Path, dest: Path):
    dest.parent.mkdir(parents=True, exist_ok=True)
    temp = dest.with_name("." + dest.name + f".{os.getpid()}.tmp")
    try:
        with checked(source).open("rb") as src, temp.open("xb") as dst:
            shutil.copyfileobj(src, dst, 1024 * 1024)
            dst.flush()
            os.fsync(dst.fileno())
        os.replace(temp, dest)
    finally:
        temp.unlink(missing_ok=True)


def capture(a):
    idn = identity(a)
    row = get_row(idn, a.owner)
    owner = str(seven.safe_owner(a.owner))
    if a.minic is None or sha(a.minic) != idn["current_minic_compiler_sha256"]:
        raise ValueError("current MiniC binary drift during successful GNU-AS owner")
    if Path(a.object).resolve() != (a.out / owner).resolve():
        raise ValueError("checkpoint object does not equal actual Kbuild output")
    # Kbuild may update its .cmd to reference the wrapper by now; the exact
    # original GNU source/.cmd SHA captured in INIT remains authoritative.
    if not (a.preprocessed.name.endswith(".minic-stage2.i") and
            a.assembly.name.endswith(".minic-stage2.s")):
        raise ValueError("not the original GNU-E -> current MiniC-S staging paths")
    shards.check_elf(a.out / owner, a.out / owner, owner, 32)
    with (a.out / owner).open("rb") as stream:
        header = stream.read(52)
    flags = struct.unpack_from("<I", header, 36)[0]
    if flags & 0x7:
        raise ValueError(f"ELF32 RISC-V C owner has RVC/hard-float flags: {owner} flags={flags:#x}")
    result = dict(object=owner, status=STATUS,
                  input_sha256=sha(a.preprocessed), asm_sha256=sha(a.assembly),
                  obj_sha256=sha(a.object), cmd_sha256=row["cmd_sha256"],
                  source_sha256=row["source_sha256"])
    base = a.partial / "staged" / owner
    atomic_stage(a.assembly, base.with_suffix(".s"))
    atomic_stage(a.object, base)
    seven.atomic_json(a.partial / "checkpoint/owners" / (owner + ".json"), result)
    print(f"RV32I_SEVEN_OWNER_CHECKPOINT=PASS owner={owner} object_sha={result['obj_sha256']}")


def preprocessed_sha(out: Path, owner: str, pinned: Path | None = None) -> str:
    # Exactly the GNU-E invocation in stage2_kbuild_cc.sh, derived from the
    # original .cmd; neither GNU -c nor MiniC -S is called on cache HIT.
    original = regen.pinned_kbuild_cc_args(pinned or out, owner)
    sources = [arg for i, arg in enumerate(original)
               if arg.endswith(".c") and (not i or original[i-1] not in ("-include", "-imacros"))]
    if len(sources) != 1:
        raise ValueError(f"ambiguous C source for resume GNU-E: {owner}")
    pp = []
    index = 0
    while index < len(original):
        value = original[index]
        if value in ("-c", "-E", "-S", sources[0]):
            index += 1
        elif value == "-o":
            index += 2
        else:
            pp.append(value)
            index += 1
    tmp = out / (owner.removesuffix(".o") + ".minic-resume-verify.i")
    if tmp.exists():
        raise ValueError(f"unsafe pre-existing resume input: {tmp}")
    try:
        subprocess.run(["riscv64-linux-gnu-gcc", *pp, "-E", "-P", "-MT", owner,
                        sources[0], "-o", str(tmp)], cwd=out, check=True,
                       capture_output=True, text=True, timeout=180)
        return sha(tmp)
    finally:
        tmp.unlink(missing_ok=True)


def verified_owners(a, previous: Path, copy: bool) -> list[str]:
    current = identity(a)
    old = seven.json_file(previous / "checkpoint/identity.json")
    if current != old:
        raise ValueError("partial resume foreign C source/compiler/GNU tools/generated header epoch")
    allowed = {r["object"] for r in current["rows"]}
    folder = previous / "checkpoint/owners"
    if not folder.is_dir() or folder.is_symlink():
        return []
    # A previous red build can have C inputs on both sides of Kbuild's real
    # generated-header transition. A cached .o is admissible ONLY when fresh
    # original GNU-E reproduces its exact old bytes. Mismatch means reject
    # that individual cache hit and force the real current MiniC/GNU-AS sweep
    # to recompile it. Never promote an old .o or silently count it as PASS.
    found = []
    for p in sorted(folder.rglob("*.json")):
        if p.is_symlink():
            raise ValueError("unsafe symlinked previous owner checkpoint")
        name = p.relative_to(folder).as_posix().removesuffix(".json")
        if name not in allowed:
            raise ValueError(f"unexpected previous partial checkpoint owner: {name}")
        row = get_row(current, name)
        result = seven.json_file(p)
        if (result.get("status") != STATUS or result.get("object") != name or
                result.get("cmd_sha256") != row["cmd_sha256"] or
                result.get("source_sha256") != row["source_sha256"]):
            raise ValueError(f"bad old per-owner compile/source receipt: {name}")
        saved = previous / "staged" / name
        for path, key in ((saved, "obj_sha256"), (saved.with_suffix(".s"), "asm_sha256")):
            if sha(path) != result.get(key):
                raise ValueError(f"tampered old successful object or assembly: {name}")
        expected_i = result.get("input_sha256")
        if not isinstance(expected_i, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_i):
            raise ValueError(f"bad old GNU-E source digest: {name}")
        if preprocessed_sha(a.out, name) != expected_i:
            print(f"RV32I_SEVEN_PARTIAL_GNU_E_MISMATCH=REBUILD_REQUIRED owner={name} "
                  "cached_object_rejected=true no_GNU_C_fallback=true")
            continue
        shards.check_elf(a.out / name, saved, name, 32)
        if copy:
            atomic_stage(saved, a.out / name)
            atomic_stage(saved, a.partial / "staged" / name)
            atomic_stage(saved.with_suffix(".s"),
                         a.partial / "staged" / Path(name).with_suffix(".s"))
            seven.atomic_json(a.partial / "checkpoint/owners" / (name + ".json"), result)
        found.append(name)
    return found


def restore(a):
    names = verified_owners(a, a.resume, copy=True)
    file = a.partial / "checkpoint/resumed-owners.txt"
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_text("".join(name + "\n" for name in names))
    print(f"RV32I_SEVEN_PARTIAL_RESUME=PASS authenticated_current_C={len(names)} "
          f"remaining_C={len(identity(a)['rows'])-len(names)}")


def replay_missing(a):
    """Compile only authenticated resume misses from their original GNU .cmd.

    A selected-target `make CC=stage2_kbuild_cc.sh` also re-enters Kconfig
    syncconfig on some clean runners. That changes autoconf.h/rustc_cfg for
    *every* C owner, invalidating the entire GNU original input epoch.
    Reuse the existing bounded direct-wrapper replay instead: it executes
    the exact frozen GNU command for each missing owner, with real GNU-E,
    current MiniC-S, GNU-AS and the wrapper's atomic capture. Never weaken
    the generated-header guard to make an incompatible Kconfig epoch pass.
    """
    idn = identity(a)
    if a.minic is None or sha(a.minic) != idn["current_minic_compiler_sha256"]:
        raise ValueError("current MiniC binary drift before direct missing-owner replay")
    if a.root is None or source_profile(a.root) != idn["current_compiler_source_profile_sha256"]:
        raise ValueError("current MiniC source profile drift before direct replay")
    wrapper = a.root / "tests/external/linux/stage2_kbuild_cc.sh"
    if sha(wrapper) != idn["stage2_wrapper_sha256"]:
        raise ValueError("stage2 compiler wrapper drift before direct replay")
    route_guard = a.root / "tools/ci/linux-runtime-minic-route-audit-v1.py"
    if sha(route_guard) != idn["stage2_route_guard_sha256"]:
        raise ValueError("MiniC/GNU-AS route guard drift before direct replay")
    pinned, selected, _snapshot = pinned_gnu(a, idn)
    if regen.generated(a.out) != idn["generated_headers"]:
        raise ValueError("GNU generated header epoch drift before direct replay")
    resumed_file = a.partial / "checkpoint/resumed-owners.txt"
    if not resumed_file.is_file() or resumed_file.is_symlink():
        raise ValueError("direct replay requires authenticated resume manifest")
    resumed = resumed_file.read_text().splitlines()
    selected_owners = owner_paths(selected)
    if len(resumed) != len(set(resumed)) or not set(resumed) <= set(selected_owners):
        raise ValueError("noncanonical or duplicate authenticated resume owners")
    for name in resumed:
        row = get_row(idn, name)
        proof = seven.json_file(a.partial / "checkpoint/owners" / (name + ".json"))
        if (proof.get("status") != STATUS or proof.get("object") != name or
                proof.get("cmd_sha256") != row["cmd_sha256"] or
                proof.get("source_sha256") != row["source_sha256"] or
                sha(a.out / name) != proof.get("obj_sha256") or
                sha(a.partial / "staged" / name) != proof.get("obj_sha256")):
            raise ValueError(f"unauthenticated restored owner before direct replay: {name}")
    missing = [name for name in selected_owners if name not in set(resumed)]
    for name in missing:
        if (a.partial / "checkpoint/owners" / (name + ".json")).exists():
            raise ValueError(f"unrestored owner already has checkpoint: {name}")
        if (a.out / name).exists():
            raise ValueError(f"unrestored owner still has stale GNU object: {name}")
    if not missing:
        print(f"RV32I_SEVEN_DIRECT_REPLAY=PASS missing=0 cached={len(resumed)}")
        return

    directory = a.partial / "checkpoint/direct-missing-replay"
    directory.mkdir(parents=True, exist_ok=True)
    env = {"MINIC_KEEP_INTERMEDIATES": "1",
           "MINIC_KBUILD_RV32I_ORIGIN_AUDIT": "1",
           "MINIC_PRESERVE_FAILURE_INPUTS": "1",
           "MINIC_KBUILD_RV32_PARTIAL_DIR": str(a.partial.resolve()),
           "MINIC_KBUILD_RV32_PARTIAL_OUT": str(a.out.resolve()),
           "MINIC_KBUILD_RV32_PARTIAL_HELPER": str(Path(__file__).resolve())}
    previous = {key: os.environ.get(key) for key in env}
    os.environ.update(env)

    def one(index: int, name: str):
        # One independent owner per call (<=16 cap). Even when a compiler
        # owner fails, workers still attempt the rest and retain successful
        # atomic checkpoints for another SHA-authenticated resume.
        selection = directory / f"{index:04d}.selected.txt"
        selection.write_text(name + "\n")
        regen.replay_affected(a.out, pinned, selection, wrapper, a.minic,
                              directory / f"{index:04d}.success.txt",
                              directory / f"{index:04d}.sha256")

    failures = []
    try:
        with ThreadPoolExecutor(max_workers=min(6, len(missing))) as pool:
            tasks = {pool.submit(one, index, name): name
                     for index, name in enumerate(missing)}
            for task in as_completed(tasks):
                name = tasks[task]
                try:
                    task.result()
                except (ValueError, OSError, regen.GuardError, subprocess.CalledProcessError) as exc:
                    failures.append(f"{name}: {exc}")
                    print(f"RV32I_SEVEN_DIRECT_REPLAY_OWNER=FAIL owner={name} reason={exc}",
                          file=sys.stderr, flush=True)
    finally:
        for key, old in previous.items():
            if old is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = old
    if regen.generated(a.out) != idn["generated_headers"]:
        raise ValueError("GNU generated header epoch drift during direct replay")
    if failures:
        raise ValueError(f"direct missing-owner MiniC replay failed {len(failures)}/{len(missing)}; "
                         f"first={failures[0]}")
    for name in missing:
        proof = seven.json_file(a.partial / "checkpoint/owners" / (name + ".json"))
        row = get_row(idn, name)
        if (proof.get("status") != STATUS or proof.get("object") != name or
                proof.get("cmd_sha256") != row["cmd_sha256"] or
                proof.get("source_sha256") != row["source_sha256"] or
                sha(a.out / name) != proof.get("obj_sha256") or
                sha(a.partial / "staged" / name) != proof.get("obj_sha256")):
            raise ValueError(f"direct replay owner lacks atomic MiniC/GNU-AS proof: {name}")
    print(f"RV32I_SEVEN_DIRECT_REPLAY=PASS rebuilt={len(missing)} cached={len(resumed)} "
          "direct_original_GNU_cmd=true Kbuild_syncconfig_avoided=true")


def verify(a):
    idn = identity(a)
    if a.minic is None or sha(a.minic) != idn["current_minic_compiler_sha256"]:
        raise ValueError("current MiniC binary drift since shard initialization")
    if a.root is None or source_profile(a.root) != idn["current_compiler_source_profile_sha256"]:
        raise ValueError("current MiniC source profile drift since shard initialization")
    expected = {r["object"] for r in idn["rows"]}
    local = a.partial / "checkpoint/owners"
    if not local.is_dir():
        print(f"RV32I_SEVEN_PARTIAL_VERIFY=PASS current_C=0 expected_C={len(expected)}")
        return
    found = set()
    for record in local.rglob("*.json"):
        name = record.relative_to(local).as_posix().removesuffix(".json")
        if name not in expected or name in found:
            raise ValueError(f"extra/duplicate selected C owner checkpoint {name}")
        row = get_row(idn, name)
        proof = seven.json_file(record)
        if proof.get("status") != STATUS or proof.get("object") != name or \
           proof.get("source_sha256") != row["source_sha256"] or \
           proof.get("cmd_sha256") != row["cmd_sha256"]:
            raise ValueError(f"bad C owner source-epoch checkpoint {name}")
        for p, key in ((a.partial / "staged" / name, "obj_sha256"),
                       (a.partial / "staged" / Path(name).with_suffix(".s"), "asm_sha256"),
                       (a.out / name, "obj_sha256")):
            if sha(p) != proof.get(key):
                raise ValueError(f"SHA drift in completed C-owner checkpoint {name}")
        found.add(name)
    live = regen.generated(a.out)
    if live != idn["generated_headers"]:
        pinned, _selected, _snapshot = pinned_gnu(a, idn)
        now, changed, affected = header_delta(a, idn)
        post = seven.json_file(a.partial / "checkpoint/post-generated-header-epoch.json")
        if post != {"schema": SCHEMA, "headers": now, "changed": changed,
                    "recompiled": affected} or now != live:
            raise ValueError("generated GNU headers drifted without authenticated dependency replay")
        # An affected owner must have a saved CURRENT GNU-E digest matching
        # the FINAL regenerated header bytes, not merely a valid old ELF.
        for name in affected:
            proof = seven.json_file(local / (name + ".json"))
            if preprocessed_sha(a.out, name, pinned) != proof.get("input_sha256"):
                raise ValueError(f"dynamic GNU header owner not replayed: {name}")
    print(f"RV32I_SEVEN_PARTIAL_VERIFY=PASS current_C={len(found)} "
          f"expected_C={len(expected)} missing_C={len(expected-found)} no_GNU_C_fallback=true")


def publish_traces(a):
    """Replace parallel Kbuild traces with exactly one final sealed SHA/owner.

    A generated-header replay can compile one C owner twice; the first hash
    is intentionally obsolete and must not masquerade as the final object.
    """
    idn = identity(a)
    successes, hashes = [], []
    for row in idn["rows"]:
        owner = row["object"]
        proof = seven.json_file(a.partial / "checkpoint/owners" / (owner + ".json"))
        if (proof.get("object") != owner or proof.get("status") != STATUS or
                proof.get("cmd_sha256") != row["cmd_sha256"] or
                proof.get("source_sha256") != row["source_sha256"] or
                sha(a.out / owner) != proof.get("obj_sha256") or
                sha(a.partial / "staged" / owner) != proof.get("obj_sha256")):
            raise ValueError(f"final per-owner MiniC/GNU-AS provenance absent: {owner}")
        successes.append(owner)
        hashes.append(f"{proof['obj_sha256']}  {owner}")
    if not a.success_trace or not a.digest_trace:
        raise ValueError("final trace publication needs two explicit output paths")
    a.success_trace.write_text("\n".join(successes) + "\n")
    a.digest_trace.write_text("\n".join(hashes) + "\n")
    print(f"RV32I_SEVEN_FINAL_TRACES=PASS current_C={len(successes)} "
          "one_final_SHA_per_owner=true")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--mode", choices=("init", "capture", "resume", "replay-missing", "reconcile", "verify", "publish-traces"), required=True)
    p.add_argument("--partial", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--root", type=Path)
    p.add_argument("--provenance", type=Path)
    p.add_argument("--selected", type=Path)
    p.add_argument("--minic", type=Path)
    p.add_argument("--source", type=Path)
    p.add_argument("--gnu-gcc", type=Path)
    p.add_argument("--gnu-as", type=Path)
    p.add_argument("--owner")
    p.add_argument("--object", type=Path)
    p.add_argument("--preprocessed", type=Path)
    p.add_argument("--assembly", type=Path)
    p.add_argument("--resume", type=Path)
    p.add_argument("--success-trace", type=Path)
    p.add_argument("--digest-trace", type=Path)
    a = p.parse_args()
    try:
        if a.mode == "init":
            initialize(a)
        elif a.mode == "capture":
            if not all((a.owner, a.object, a.preprocessed, a.assembly)):
                raise ValueError("capture requires actual Kbuild owner, GNU-E and MiniC-S paths")
            capture(a)
        elif a.mode == "resume":
            if a.resume is None:
                raise ValueError("resume requires previous partial candidate artifact")
            restore(a)
        elif a.mode == "replay-missing":
            replay_missing(a)
        elif a.mode == "reconcile":
            reconcile(a)
        elif a.mode == "publish-traces":
            publish_traces(a)
        else:
            verify(a)
        return 0
    except (ValueError, OSError, regen.GuardError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired, json.JSONDecodeError) as exc:
        print(f"RV32I_SEVEN_PARTIAL=FAIL mode={a.mode} reason={exc}", file=sys.stderr)
        return 8


if __name__ == "__main__":
    raise SystemExit(main())
