#!/usr/bin/env python3
"""Fail-closed Linux seven-runner partial-resume and process-tree regression."""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time


def load(filename, name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_child_killed(candidate, root):
    pidfile = root / "child.pid"
    command = [sys.executable, "-c",
               "import subprocess,sys,time; "
               "child=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)']); "
               "open(sys.argv[1],'w').write(str(child.pid));time.sleep(60)",
               str(pidfile)]
    try:
        candidate.run_owner_process(command, timeout=1)
        raise AssertionError("hung MiniC driver was accepted")
    except subprocess.TimeoutExpired:
        pass
    child = int(pidfile.read_text())
    for _ in range(20):
        proc = Path(f"/proc/{child}/stat")
        try:
            state = proc.read_text().split(") ", 1)[1][0]
        except (FileNotFoundError, ProcessLookupError):
            # The timed-out subprocess may exit between a path-exists
            # probe and the actual /proc read. ESRCH/ENOENT both prove that
            # this particular PID is no longer a running child.
            break
        if state == "Z":
            break
        time.sleep(0.05)
    else:
        raise AssertionError(f"timed-out MiniC driver left child {child} running")


def test_atomic_resume(candidate, seven, root):
    binary = root / "bin"
    binary.mkdir()
    for name in ("minic", "minic-cc"):
        executable = binary / name
        executable.write_text("#!/bin/sh\nexit 0\n")
        executable.chmod(0o755)
    a = argparse.Namespace(minic=binary / "minic", target="x86_64",
                           resume=None, output=root / "output")
    original = {"schema": "minic-three-isa-seven-input-v1",
                "target": "x86_64", "frozen_manifest_sha256": "a" * 64,
                "original_config_sha256": "b" * 64}
    identity = seven.checkpoint_identity(a, candidate, original)
    expected = root / "prior"
    seven.atomic_json(expected / "checkpoint/identity.json", identity)
    owner = "kernel/checkpoint.o"
    source = expected / "staged/kernel/checkpoint.s"
    target = expected / "staged/kernel/checkpoint.o"
    source.parent.mkdir(parents=True)
    source.write_text(".text\n")
    elf = bytearray(64)
    elf[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<HH", elf, 16, 1, 62)
    target.write_bytes(elf)
    result = {"object": owner, "input_sha256": "c" * 64,
              "status": "MINIC_S_GNU_AS_PASS", "asm_sha256": seven.file_sha(source),
              "obj_sha256": seven.file_sha(target)}
    seven.checkpoint_owner(expected, result)
    rows = [{"object": owner, "sha256": "c" * 64}]
    a.resume = expected
    resumed = seven.resume_owners(a, candidate, rows, identity)
    assert resumed[owner] == result
    assert seven.file_sha(a.output / "staged" / owner) == result["obj_sha256"]
    # Independent file SHA is mandatory, even with a syntactically valid ELF.
    target.write_bytes(bytes(elf) + b"corruption")
    try:
        seven.resume_owners(a, candidate, rows, identity)
        raise AssertionError("tampered partial object accepted")
    except ValueError:
        pass
    # Never reuse objects if just the compiler implementation changed.
    (binary / "minic-cc").write_text("#!/bin/sh\nexit 1\n")
    mutated = seven.checkpoint_identity(a, candidate, original)
    try:
        seven.resume_owners(a, candidate, rows, mutated)
        raise AssertionError("other compiler epoch accepted")
    except ValueError:
        pass


def test_owner_streaming_real_gnu_as(candidate, root):
    source = root / "stream-input"
    build = source / "build"
    frozen = source / "frozen/i/kernel"
    build.mkdir(parents=True)
    frozen.mkdir(parents=True)
    script = root / "fake-minic"
    script.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib,sys\n"
        "out=pathlib.Path(sys.argv[sys.argv.index('-o')+1])\n"
        "out.write_text('.text\\n.globl minic_owner\\nminic_owner:\\n ret\\n')\n")
    script.chmod(0o755)
    rows = []
    for name in ("one", "two", "three"):
        src = frozen / (name + ".i")
        src.write_text("int " + name + ";\n")
        rows.append({"object": "kernel/" + name + ".o",
                     "input": "i/kernel/" + name + ".i",
                     "sha256": candidate.sha(src),
                     "minic_flags": [], "gnu_as_flags": []})
    data = {"target": "x86_64", "gcc": "gcc", "machine": 62}
    out1 = root / "first-compile"
    writes = []
    results = candidate.make_objects(rows, source, script, out1, data, 2, 10,
                                     on_result=lambda result: writes.append(result))
    assert len(writes) == 3 and all(x["status"] == "MINIC_S_GNU_AS_PASS" for x in results)
    out2 = root / "resumed-compile"
    first = rows[0]["object"]
    for suffix in (".s", ".o"):
        p = Path(first).with_suffix(suffix)
        dest = out2 / "staged" / p
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(out1 / "staged" / p, dest)
    replayed = candidate.make_objects(rows, source, script, out2, data, 2, 10,
                                     resumed={first: results[0]})
    assert replayed[0] == results[0] and len(replayed) == 3
    assert all(result["status"] == "MINIC_S_GNU_AS_PASS" for result in replayed)


def main():
    candidate = load("linux-three-isa-minic-candidate-v1.py", "candidate_partial_test")
    seven = load("linux-three-isa-seven-runtime-v1.py", "seven_partial_test")
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        test_child_killed(candidate, root)
        test_atomic_resume(candidate, seven, root)
        test_owner_streaming_real_gnu_as(candidate, root)
    print("SEVEN_PARTIAL_REGRESSION=PASS process_group_kill=true "
          "atomic_owners=true sha_reauthentication=true compiler_epoch_rejected=true "
          "real_gnu_assembler=true resumed_owners=true")


if __name__ == "__main__":
    main()
