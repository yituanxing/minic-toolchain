#!/usr/bin/env python3
"""Cheap, toolchain-free contract check for source-bound native bootstrap seeds.

Fixture files are synthetic and prove only the verifier's accept/reject logic.
They are not Stage4 self-host evidence or substitutes for the QEMU acceptance.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def module_from(path, name):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return digest(data)


def require_rejected(native, stage2, artifact, work, sources, reason):
    try:
        native.verify_seed(stage2, artifact, work, sources)
    except RuntimeError as exc:
        if reason not in str(exc):
            raise AssertionError("wrong rejection: " + str(exc)) from exc
    else:
        raise AssertionError("accepted bad bootstrap seed: " + reason)


def main():
    native = module_from("tools/ci/minic-bootstrap-full-native-v1.py", "bootstrap_native")
    tools = module_from("tools/ci/minic-bootstrap-stage2-toolchain-v1.py", "bootstrap_plan")
    sources, _ = native.inventory()
    plan, owners = tools.source_plan(tools.CANONICAL)
    with tempfile.TemporaryDirectory(prefix="minic-seed-contract-") as tmp:
        work = Path(tmp) / "bootstrap"
        artifact = Path(tmp) / "tools"
        stage0 = work / "stage0/bin/minic-cc"
        stage1 = work / "seed/minic-stage1-rv64"
        stage2 = work / "stage2/minic-stage2-rv64"
        h0 = write(stage0, b"SYNTHETIC_HOST_STAGE0")
        h1 = write(stage1, b"SYNTHETIC_RV64_STAGE1")
        h2 = write(stage2, b"SYNTHETIC_RV64_STAGE2")
        load_hash = write(work / "stage2/minic-stage1.load.bin", b"SAME_IMAGE")
        write(work / "stage2/minic-stage2.load.bin", b"SAME_IMAGE")
        for path, expected in ((work / "seed/stage0-binary.sha256", h0),
                               (work / "seed/stage1-binary.sha256", h1),
                               (work / "stage2/stage2-binary.sha256", h2)):
            write(path, (expected + "  synthetic\n").encode())
        write(work / "stage2/stage2-fixed-point.txt", (
            "stage1_elf_sha256=" + h1 + "\n" +
            "stage2_elf_sha256=" + h2 + "\n" +
            "loadable_image_sha256=" + load_hash + "\n" +
            "source_count=" + str(len(sources["minic-cc"])) + "\n").encode())
        write(work / "seed/sources.normalized", (
            "\n".join(sources["minic-cc"]) + "\n").encode())
        commit = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
            stdout=subprocess.PIPE, text=True).stdout.strip()
        write(work / "seed/source-commit.txt", (commit + "\n").encode())
        source_hashes = ("\n".join(
            src + "\t" + native.sha(ROOT / src) for src in sources["minic-cc"]) + "\n").encode()
        write(work / "seed/source-sha256.tsv", source_hashes)

        stage0_rows = []
        input_rows = []
        for src in sources["minic-cc"]:
            key = src.replace("/", "__").replace(".", "_")
            ih = write(work / "seed/i" / (key + ".i"), (src + ":seed-i").encode())
            sh = write(work / "seed/s" / (key + ".s"), (src + ":seed-s").encode())
            write(work / "stage2/s" / (key + ".s"), (src + ":seed-s").encode())
            stage0_rows.append(key + "\t" + src + "\t" + sh)
            input_rows.append(key + "\t" + src + "\t" + ih)
        write(work / "seed/stage0-manifest.tsv", ("\n".join(stage0_rows) + "\n").encode())
        write(work / "seed/input-sha256.tsv", ("\n".join(input_rows) + "\n").encode())

        rows = []
        binaries = {}
        for src in owners:
            key = src.replace("/", "__").replace(".", "_")
            ih = write(artifact / "i" / (key + ".i"), (src + ":tool-i").encode())
            sh = write(artifact / "s/stage0" / (key + ".s"), (src + ":tool-s").encode())
            write(artifact / "s/stage2" / (key + ".s"), (src + ":tool-s").encode())
            rows.append(dict(source=src, input_sha256=ih, asm_sha256=sh))
        for name in tools.CANONICAL:
            bh = write(artifact / "bin" / name, (name + ":elf").encode())
            binaries[name] = dict(sha256=bh,
                                  linked_sources=len(plan["#" + name]["sources"]),
                                  help_rc=0)
        write(artifact / "summary.json", json.dumps(dict(
            selected_tools=list(tools.CANONICAL), stage0_sha256=h0,
            stage2_sha256=h2, tus=rows, binaries=binaries)).encode())
        bound = native.verify_seed(stage2, artifact, work, sources)
        if len(bound) != 9:
            raise AssertionError("not all nine native executables bound")
        print("BOOTSTRAP_SOURCE_BINDING_SYNTHETIC=PASS compiler_tus=%d tool_tus=%d tools=%d" %
              (len(sources["minic-cc"]), len(owners), len(bound)), flush=True)

        manifest = work / "seed/source-sha256.tsv"
        write(manifest, source_hashes.replace(b"\t", b"\t0", 1))
        require_rejected(native, stage2, artifact, work, sources,
                         "current source contents differ")
        write(manifest, source_hashes)
        write(work / "seed/source-commit.txt", ("0" * len(commit) + "\n").encode())
        require_rejected(native, stage2, artifact, work, sources,
                         "source commit differs")
        write(work / "seed/source-commit.txt", (commit + "\n").encode())
        elf = artifact / "bin/minic-ld"
        write(elf, b"TAMPERED ELF")
        require_rejected(native, stage2, artifact, work, sources,
                         "ELF/owner SHA minic-ld")
        print("BOOTSTRAP_SOURCE_BINDING_NEGATIVE=PASS stale_source=true stale_commit=true tampered_tool=true",
              flush=True)


if __name__ == "__main__":
    main()
