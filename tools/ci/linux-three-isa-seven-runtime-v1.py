#!/usr/bin/env python3
"""Seven *independent runners* for original GNU Linux -> MiniC/GNU AS -> GNU LD/QEMU.

`export` runs only after exact-GNU P0/P1 on the pristine GNU cache.
`produce` consumes one immutable GNU -E C-owner shard, with no GNU Kbuild tree.
`receive` merges seven authenticated C-owner object shards into a disposable
GNU cache and delegates unmodified original Kbuild relink and QEMU comparison
to linux-three-isa-minic-candidate-v1.py. No GCC C object fallback exists.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys


def candidate():
    import importlib.util
    path = Path(__file__).with_name("linux-three-isa-minic-candidate-v1.py")
    spec = importlib.util.spec_from_file_location("minic_seven_strict_candidate", path)
    if spec is None or spec.loader is None:
        raise ValueError("missing strict candidate oracle")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def file_sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            digest.update(chunk)
    return digest.hexdigest()


def json_file(path: Path) -> dict:
    if not path.is_file() or path.is_symlink():
        raise ValueError(f"missing or unsafe provenance JSON: {path}")
    return json.loads(path.read_text())


def safe_owner(name: str) -> PurePosixPath:
    path = PurePosixPath(name)
    if (path.is_absolute() or ".." in path.parts or
            len(path.parts) < 2 or path.suffix != ".o"):
        raise ValueError(f"unsafe original GNU C-owner path: {name!r}")
    return path


def verify_partition(rows: list[dict], shard: int, full_count: int) -> None:
    if (not 0 <= shard < 7 or full_count < 200 or
            len(rows) != len(range(shard, full_count, 7))):
        raise ValueError("incomplete seven-runner GNU C-owner partition")
    for index, row in zip(range(shard, full_count, 7), rows):
        if row.get("index") != index or row.get("shard") != shard:
            raise ValueError("mismatched frozen GNU C-owner index/shard")
        safe_owner(row["object"])
        if (not isinstance(row.get("sha256"), str) or
                re.fullmatch(r"[0-9a-f]{64}", row["sha256"]) is None or
                not isinstance(row.get("cmd_sha256"), str) or
                re.fullmatch(r"[0-9a-f]{64}", row["cmd_sha256"]) is None):
            raise ValueError("malformed original GNU .i/.cmd digest")


def original_incbin_resources(build: Path, source: Path) -> dict[str, Path]:
    """Ship only original build-relative binary inputs, not another GNU tree.

    Linux's original config-data object uses an inline GAS `.incbin` filename.
    We also discover literal relative references in the exact frozen GNU -E
    input. An unresolved referenced resource must fail before any MiniC run.
    """
    text = source.read_text(errors="replace")
    names = set(re.findall(r'\.incbin\s*\\?["\']([^"\'\\]+)', text))
    if "kernel/config_data.gz" in text:
        names.add("kernel/config_data.gz")
    resources: dict[str, Path] = {}
    for name in names:
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError(f"unsafe GNU assembler build-relative resource {name}")
        original = build / path
        if not original.is_file() or original.is_symlink():
            raise ValueError(f"missing original GNU .incbin resource {name}")
        resources[str(path)] = original
    return resources


def export(a: argparse.Namespace, c) -> None:
    if a.output.exists():
        raise ValueError("refuse to reuse original GNU shard-export directory")
    golden = json_file(a.gnu_receipt)
    rows, data = c.candidate_inputs(a.target, a.gnu_work, a.minic,
                                    golden, a.initramfs)
    full_manifest = c.required(a.gnu_work / "frozen/manifest.json")
    frozen_manifest_sha256 = file_sha(full_manifest)
    gnu_receipt_sha256 = file_sha(a.gnu_receipt)
    source = a.gnu_work / "frozen"
    build = a.gnu_work / "build"
    for shard in range(7):
        subset = rows[shard::7]
        verify_partition(subset, shard, len(rows))
        root = a.output / str(shard)
        (root / "frozen").mkdir(parents=True)
        shutil.copy2(full_manifest, root / "frozen/manifest.json")
        for relative in ("build/.config", "evidence/full-c-owners.txt"):
            source_file = c.required(a.gnu_work / relative)
            target_file = root / relative
            target_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_file, target_file)
        external: dict[str, str] = {}
        for row in subset:
            name = safe_owner(row["object"])
            inp = PurePosixPath(row["input"])
            if inp.is_absolute() or ".." in inp.parts or inp.parts[0] != "i":
                raise ValueError("unsafe frozen source path")
            original = c.required(source / inp)
            if file_sha(original) != row["sha256"]:
                raise ValueError(f"corrupt original GNU -E: {row['object']}")
            copy_to = root / "frozen" / inp
            copy_to.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, copy_to)
            command_file = c.required(build / name.parent / ("." + name.name + ".cmd"))
            if file_sha(command_file) != row["cmd_sha256"]:
                raise ValueError(f"corrupt original GNU .o.cmd: {row['object']}")
            command_copy = root / "build" / name.parent / ("." + name.name + ".cmd")
            command_copy.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(command_file, command_copy)
            for relative, resource in original_incbin_resources(build, original).items():
                external[relative] = file_sha(resource)
                resource_copy = root / "build" / relative
                resource_copy.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(resource, resource_copy)
        identity = {"schema": "minic-three-isa-seven-input-v1",
                    "target": a.target, "shard": shard,
                    "total_c_owners": len(rows), "shard_c_owners": len(subset),
                    "frozen_manifest_sha256": frozen_manifest_sha256,
                    "gnu_receipt_sha256": gnu_receipt_sha256,
                    "original_gnu_image_sha256": golden["image_sha256"],
                    "original_config_sha256": file_sha(build / ".config"),
                    "original_owner_list_sha256": file_sha(a.gnu_work / "evidence/full-c-owners.txt"),
                    "gnu_c_owner_manifest_sha256": data["gnu_c_owner_manifest_sha256"],
                    "compiler_driver_sha256": data["compiler_driver_sha256"],
                    "compiler_cc_sha256": data["compiler_cc_sha256"],
                    "external_build_resources": external}
        (root / "identity.json").write_text(json.dumps(identity, indent=2,
                                                       sort_keys=True) + "\n")
        print(f"THREE_ISA_SEVEN_EXPORT=PASS target={a.target} shard={shard} "
              f"owners={len(subset)} resources={len(external)}")


def verify_input(root: Path, target: str, shard: int, c) -> tuple[list[dict], dict]:
    identity = json_file(root / "identity.json")
    if (identity.get("schema") != "minic-three-isa-seven-input-v1" or
            identity.get("target") != target or identity.get("shard") != shard):
        raise ValueError("foreign target/shard GNU -E artifact")
    manifest = root / "frozen/manifest.json"
    if file_sha(c.required(manifest)) != identity["frozen_manifest_sha256"]:
        raise ValueError("whole frozen GNU C-owner manifest SHA mismatch")
    entire = json_file(manifest)["rows"]
    if len(entire) != identity["total_c_owners"]:
        raise ValueError("invalid complete GNU original C-owner census")
    rows = entire[shard::7]
    verify_partition(rows, shard, len(entire))
    if len(rows) != identity["shard_c_owners"]:
        raise ValueError("partial original GNU C-owner artifact")
    if (file_sha(c.required(root / "build/.config")) != identity["original_config_sha256"] or
            file_sha(c.required(root / "evidence/full-c-owners.txt")) != identity["original_owner_list_sha256"]):
        raise ValueError("original GNU config/owner-list checksum changed")
    for row in rows:
        name = safe_owner(row["object"])
        source = c.required(root / "frozen" / row["input"])
        cmd = c.required(root / "build" / name.parent / ("." + name.name + ".cmd"))
        if file_sha(source) != row["sha256"] or file_sha(cmd) != row["cmd_sha256"]:
            raise ValueError(f"source/command SHA changed in shard {name}")
    for relative, digest in identity["external_build_resources"].items():
        path = PurePosixPath(relative)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError("unsafe GNU assembly resource path")
        if file_sha(c.required(root / "build" / path)) != digest:
            raise ValueError("original GNU .incbin resource changed")
    return rows, identity


def produce(a: argparse.Namespace, c) -> None:
    if a.output.exists():
        raise ValueError("refuse to reuse candidate object shard output")
    rows, identity = verify_input(a.input, a.target, a.shard, c)
    compiler = c.compiler_pair(a.minic)
    # Follow the verified RV/MiniAS frozen-source replay: original GNU -E
    # inputs are independent of the *current* MiniC revision. The source
    # producer's compiler SHA remains an audit field, never a cache gate.
    # Current compiler SHA is instead bound to every emitted object receipt.
    data = {"target": a.target, "gcc": c.TARGETS[a.target][2],
            "machine": c.TARGETS[a.target][5]}
    a.output.mkdir(parents=True)
    results = c.make_objects(rows, a.input, a.minic, a.output,
                             data, a.workers, a.timeout)
    (a.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    failures = [row for row in results if row["status"] != "MINIC_S_GNU_AS_PASS"]
    if failures:
        # Historical RV seven-way replay kept *all* per-owner results and a
        # bounded first-failure reproduction even when a shard was red.
        # Never make the other six independent workers wait for this shard.
        try:
            c.preserve_first_failed_owners(a.input, a.output, rows, results)
        except (OSError, ValueError) as exc:
            print(f"THREE_ISA_SEVEN_FAILURE_DIAGNOSTIC_ERROR={exc}", file=sys.stderr)
        print(f"THREE_ISA_SEVEN_FAILURE_CENSUS target={a.target} shard={a.shard} "
              f"failures={len(failures)}/{len(rows)} "
              f"owners={[row['object'] for row in failures[:12]]}", file=sys.stderr)
        raise ValueError(f"incomplete MiniC/GNU AS shard {a.target}/{a.shard}: "
                         f"{len(rows)-len(failures)}/{len(rows)} first={failures[0]}")
    if c.compiler_pair(a.minic) != compiler:
        raise ValueError("MiniC binary changed during C-owner compilation")
    package_shard(a, c, results, identity)
    print(f"THREE_ISA_SEVEN_PRODUCE=PASS target={a.target} shard={a.shard} "
          f"MiniC_C_GNU_AS={len(rows)}/{len(rows)} no_GNU_C_fallback=1")


def gnu_as_identity(c, target: str) -> dict:
    gcc = c.TARGETS[target][2]
    prefix = c.TARGETS[target][1]
    assembler = prefix + "as" if prefix else "as"
    return {"gnu_gcc_binary_sha256": file_sha(trusted_gnu_driver_binary(gcc)),
            "gnu_as_binary_sha256": file_sha(trusted_gnu_driver_binary(assembler))}


def trusted_gnu_driver_binary(name: str) -> Path:
    """Hash the actual distro tool; /usr/bin/*-gcc is often a symlink.

    Only this trusted PATH-resolved executable is canonicalized. Frozen GNU
    inputs, candidate objects and cache entries remain symlink-rejected.
    """
    executable = shutil.which(name)
    if executable is None:
        raise ValueError(f"target GNU assembler compiler missing: {name}")
    resolved = Path(executable).resolve(strict=True)
    if not resolved.is_file() or not resolved.stat().st_size:
        raise ValueError(f"invalid installed GNU compiler executable: {name}")
    return resolved


def package_shard(a: argparse.Namespace, c, results: list[dict], identity: dict) -> None:
    compiler = c.compiler_pair(a.minic)
    artifacts = a.output / "bundle"
    artifacts.mkdir()
    # Retain all target GNU AS .o and a small strict diagnostic subset of .s;
    # GNU vDSO diagnostics require its original MiniC assembly too.
    for row in results:
        name = safe_owner(row["object"])
        obj = c.required(a.output / "staged" / name)
        if file_sha(obj) != row["obj_sha256"]:
            raise ValueError(f"producer .o changed: {name}")
        dest = artifacts / "objects" / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(obj, dest)
        if "vdso" in name.parts:
            asm = c.required(a.output / "staged" / name.with_suffix(".s"))
            dst = artifacts / "assembly" / name.with_suffix(".s")
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(asm, dst)
    (artifacts / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    (artifacts / "identity.json").write_text(json.dumps({
        **identity, **compiler,
        "schema": "minic-three-isa-seven-objects-v1",
        **gnu_as_identity(c, a.target),
    }, indent=2, sort_keys=True) + "\n")


def cache_identity(a: argparse.Namespace, c):
    """Reuse historical RV pattern: exact source/flags/compiler/AS keyed shard.

    The separate conservative cache helper deliberately disables caching
    owners with .incbin; such owners always compile afresh from their genuine
    transported original GNU assembly resources.
    """
    rows, _ = verify_input(a.input, a.target, a.shard, c)
    helper = c.module("minic_gnu_shard_cache", "linux-three-isa-candidate-shard-cache-v1.py")
    gcc = c.TARGETS[a.target][2]
    prefix = c.TARGETS[a.target][1]
    assembler = prefix + "as" if prefix else "as"
    # The cache stores GNU-AS-produced .o; its identity must include GNU AS,
    # not just the GCC driver used for the frozen original GNU preprocessing.
    as_path = trusted_gnu_driver_binary(assembler)
    identity = helper.identity(a.target, a.minic, as_path,
                               a.input / "build/.config",
                               a.input / "evidence/full-c-owners.txt",
                               json_file(a.input / "frozen/manifest.json")["rows"],
                               a.input / "frozen", a.input / "build", a.shard)
    return helper, identity


def cache_key(a: argparse.Namespace, c) -> None:
    if not a.output:
        raise ValueError("cache-key requires --output key-info JSON")
    status = {"cacheable": False, "key": ""}
    try:
        helper, identity = cache_identity(a, c)
        status = {"cacheable": True, "key": helper.key(identity), "identity": identity}
    except (ValueError, OSError, KeyError) as exc:
        # Missing mandatory source/tool identity is fatal. Explicit .incbin
        # causes only a cache MISS; compilation itself remains mandatory.
        if "external assembly resources" not in str(exc):
            raise
        status["cache_disabled_reason"] = "source_contains_external_assembly_resources"
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(status, indent=2, sort_keys=True) + "\n")
    print(f"THREE_ISA_SEVEN_CACHE_IDENTIFIED target={a.target} shard={a.shard} "
          f"cacheable={status['cacheable']}")


def cache_store(a: argparse.Namespace, c) -> None:
    expected = json_file(a.cache_identity)
    if not expected.get("cacheable"):
        raise ValueError("cache-store forbidden for resource-dependent owners")
    helper, identity = cache_identity(a, c)
    if identity != expected["identity"] or helper.key(identity) != expected["key"]:
        raise ValueError("stale MiniC candidate cache identity")
    results = json_file(a.output / "results.json")
    helper.store(a.cache, identity, a.output / "staged", results)
    print(f"THREE_ISA_SEVEN_CACHE_STORE=PASS target={a.target} shard={a.shard}")


def cache_load(a: argparse.Namespace, c) -> None:
    expected = json_file(a.cache_identity)
    if not expected.get("cacheable"):
        raise ValueError("candidate cache restore unavailable for external assembly")
    helper, identity = cache_identity(a, c)
    if identity != expected["identity"] or helper.key(identity) != expected["key"]:
        raise ValueError("stale MiniC candidate cache restore identity")
    manifest = helper.verify(a.cache, identity)
    if a.output.exists():
        raise ValueError("refuse stale candidate cache output")
    (a.output / "staged").mkdir(parents=True)
    for row in identity["rows"]:
        name = safe_owner(row["object"])
        for ext in (".o", ".s"):
            src = a.cache / "staged" / name.with_suffix(ext)
            dst = a.output / "staged" / name.with_suffix(ext)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
    results = []
    for row, entry in zip(identity["rows"], manifest["entries"]):
        results.append({"object": row["object"], "input_sha256": row["sha256"],
                        "status": "MINIC_S_GNU_AS_PASS",
                        "asm_sha256": entry["asm_sha256"],
                        "obj_sha256": entry["obj_sha256"]})
    (a.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    # Never accept cached ELF as runtime success: still emit seven real
    # authentic MiniC-owned object artifacts for separate GNU LD/QEMU.
    package_shard(a, c, results, identity_from_input(a.input))
    print(f"THREE_ISA_SEVEN_CACHE_RESTORE=PASS target={a.target} shard={a.shard} "
          f"owners={len(results)} runtime_certified=false")


def identity_from_input(root: Path) -> dict:
    return json_file(root / "identity.json")


def collect(a: argparse.Namespace, c, rows: list[dict], identity: dict) -> list[dict]:
    all_results = []
    path_taken = set()
    expected_by_name = {r["object"]: r for r in rows}
    for shard in range(7):
        artifact = a.incoming / f"minic-linux-seven-objects-{a.target}-{shard}"
        receipt = json_file(artifact / "identity.json")
        if (receipt.get("schema") != "minic-three-isa-seven-objects-v1" or
                receipt.get("target") != a.target or receipt.get("shard") != shard or
                receipt.get("frozen_manifest_sha256") != identity["frozen_manifest_sha256"] or
                receipt.get("gnu_receipt_sha256") != identity["gnu_receipt_sha256"] or
                receipt.get("gnu_c_owner_manifest_sha256") != identity["gnu_c_owner_manifest_sha256"] or
                receipt.get("original_gnu_image_sha256") != identity["original_gnu_image_sha256"] or
                receipt.get("total_c_owners") != len(rows)):
            raise ValueError(f"foreign or incomplete C-object shard receipt {shard}")
        compiler = c.compiler_pair(a.minic)
        if any(receipt.get(k) != compiler[k] for k in compiler):
            raise ValueError(f"MiniC compiler SHA drift in worker {shard}")
        if any(receipt.get(k) != value
               for k, value in gnu_as_identity(c, a.target).items()):
            raise ValueError(f"GNU target gcc/as toolchain drift in worker {shard}")
        subset = rows[shard::7]
        verify_partition(subset, shard, len(rows))
        results = json_file(artifact / "results.json")
        if not isinstance(results, list) or len(results) != len(subset):
            raise ValueError(f"partial C-object results from shard {shard}")
        for frozen_row, result in zip(subset, results):
            name = safe_owner(frozen_row["object"])
            if (result.get("object") != frozen_row["object"] or
                    result.get("input_sha256") != frozen_row["sha256"] or
                    result.get("status") != "MINIC_S_GNU_AS_PASS" or
                    not isinstance(result.get("obj_sha256"), str) or
                    not isinstance(result.get("asm_sha256"), str) or
                    name in path_taken):
                raise ValueError(f"incomplete or duplicate MiniC C owner {name}")
            path_taken.add(name)
            original = c.required(artifact / "objects" / name)
            if file_sha(original) != result["obj_sha256"]:
                raise ValueError(f"shard .o SHA mismatch {name}")
            c.elf_machine(original, c.TARGETS[a.target][5])
            c.validate_original_abi(original, a.target, frozen_row)
            dst = a.output / "staged" / name
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, dst)
            if "vdso" in name.parts:
                asm = c.required(artifact / "assembly" / name.with_suffix(".s"))
                if file_sha(asm) != result["asm_sha256"]:
                    raise ValueError(f"shard .s SHA mismatch {name}")
                staged_asm = a.output / "staged" / name.with_suffix(".s")
                shutil.copy2(asm, staged_asm)
            all_results.append(result)
    if len(all_results) != len(expected_by_name) or set(map(str, path_taken)) != set(expected_by_name):
        raise ValueError("one or more GNU C owners were not replaced by MiniC")
    # The original candidate's link-failure provenance pairs GNU source rows
    # and MiniC results by *canonical census index*, not worker completion.
    # Restore exactly that order after seven independently scheduled runners.
    by_name = {result["object"]: result for result in all_results}
    return [by_name[row["object"]] for row in rows]


def receive(a: argparse.Namespace, c) -> None:
    if a.output.exists():
        raise ValueError("refuse stale seven-runner receiver output")
    a.output.mkdir(parents=True)
    outcome = {"schema": "minic-three-isa-seven-receiver-v1", "target": a.target,
               "verdict": "MINIC_ALL_C_GNU_LINK_QEMU_NOT_CERTIFIED"}
    try:
        golden = json_file(a.gnu_receipt)
        rows, data = c.candidate_inputs(a.target, a.gnu_work, a.minic,
                                        golden, a.initramfs)
        identity = {"frozen_manifest_sha256": file_sha(c.required(a.gnu_work / "frozen/manifest.json")),
                    "gnu_receipt_sha256": file_sha(a.gnu_receipt),
                    "gnu_c_owner_manifest_sha256": data["gnu_c_owner_manifest_sha256"],
                    "original_gnu_image_sha256": golden["image_sha256"]}
        shutil.copy2(a.gnu_receipt, a.output / "gnu-qemu-receipt.json")
        rows_results = collect(a, c, rows, identity)
        (a.output / "all-c-owners-result.json").write_text(json.dumps(
            rows_results, indent=2) + "\n")
        image = c.relink(a.gnu_work, a.output, rows, rows_results, data, golden)
        c.compiler_pair(a.minic, data)
        c.boot_and_compare(data, a.output, a.initramfs, a.firmware,
                           image, golden, a.gnu_boot_logs)
        if data["gnu_c_option_equivalence"] != "NO_UNQUALIFIED_GNU_CODEGEN_OPTIONS":
            outcome["verdict"] = "MINIC_QEMU_MATCH_GNU_C_OPTIONS_INCONCLUSIVE"
            rc = 2
        else:
            outcome["verdict"] = "MINIC_ALL_C_GNU_LINK_QEMU_STRICT_DIFF_PASS"
            rc = 0
        outcome.update(data)
        outcome["owners"] = len(rows_results)
        outcome["candidate_image_sha256"] = file_sha(image)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        outcome.update(verdict="MINIC_ALL_C_GNU_LINK_QEMU_FAIL", error=str(exc))
        rc = 2
    (a.output / "candidate-receipt.json").write_text(json.dumps(outcome,
                                                                 indent=2, sort_keys=True) + "\n")
    print(f"THREE_ISA_SEVEN_RECEIVER={outcome['verdict']} target={a.target} "
          f"owners={outcome.get('owners', 0)}")
    if rc:
        raise ValueError(f"seven-runner Linux runtime not certified: {outcome.get('error', outcome['verdict'])}")


def self_test() -> None:
    fake = [dict(index=i, object=f"kernel/example_{i}.o", shard=i % 7,
                 sha256="a" * 64, cmd_sha256="b" * 64)
            for i in range(210)]
    for shard in range(7):
        verify_partition(fake[shard::7], shard, len(fake))
    for mutation in (lambda rows: rows.pop(),
                     lambda rows: rows.__setitem__(0, {**rows[0], "shard": 0})):
        bad = [dict(row) for row in fake[1::7]]
        mutation(bad)
        try:
            verify_partition(bad, 1, len(fake))
        except ValueError:
            pass
        else:
            raise AssertionError("unsafe or partial frozen owner shard accepted")
    try:
        safe_owner("../../evil.o")
    except ValueError:
        pass
    else:
        raise AssertionError("unsafe original GNU ELF owner accepted")
    print("THREE_ISA_SEVEN_RUNTIME_SELFTEST=PASS strict_shards=7")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("export", "produce", "receive", "cache-key", "cache-store", "cache-load", "selftest"))
    parser.add_argument("--target", choices=("x86_64", "aarch64", "loongarch64"))
    parser.add_argument("--shard", type=int)
    parser.add_argument("--gnu-work", type=Path)
    parser.add_argument("--gnu-receipt", type=Path)
    parser.add_argument("--gnu-boot-logs", type=Path)
    parser.add_argument("--initramfs", type=Path)
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--input", type=Path)
    parser.add_argument("--incoming", type=Path)
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--cache-identity", type=Path)
    parser.add_argument("--minic", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--workers", type=int, default=2)
    a = parser.parse_args()
    if a.mode == "selftest":
        self_test()
        return
    if not a.target or not a.minic or not a.output:
        parser.error("--target --minic --output required")
    if a.mode in ("export", "receive") and not all((a.gnu_work, a.gnu_receipt, a.initramfs)):
        parser.error("export/receive require original --gnu-work --gnu-receipt --initramfs")
    if a.mode in ("produce", "cache-key", "cache-load", "cache-store") and (
            not a.input or a.shard is None or not 0 <= a.shard < 7 or
            not 1 <= a.workers <= 8):
        parser.error("shard modes require --input --shard 0..6 --workers 1..8")
    if a.mode in ("cache-store", "cache-load") and not all((a.cache, a.cache_identity)):
        parser.error("cache-store/cache-load require --cache --cache-identity")
    if a.mode == "receive" and (not a.incoming or not a.gnu_boot_logs):
        parser.error("receive requires --incoming --gnu-boot-logs")
    c = candidate()
    if a.mode == "export":
        export(a, c)
    elif a.mode == "produce":
        produce(a, c)
    elif a.mode == "cache-key":
        cache_key(a, c)
    elif a.mode == "cache-store":
        cache_store(a, c)
    elif a.mode == "cache-load":
        cache_load(a, c)
    else:
        receive(a, c)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print(f"THREE_ISA_SEVEN_RUNTIME=FAIL {error}", file=sys.stderr)
        raise SystemExit(2) from error
