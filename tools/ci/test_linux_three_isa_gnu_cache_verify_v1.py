#!/usr/bin/env python3
"""Offline independent cache-restore attestation and corruption regression."""
import importlib.util
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest


ROOT = Path(__file__).parent


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


AUDIT = load("minic_cache_audit", "linux-three-isa-gnu-cache-verify-v1.py")
GOLDEN = load("minic_cache_golden_test", "linux-three-isa-golden-v1.py")
FREEZE = load("minic_cache_frozen_test", "linux-three-isa-freeze-i-v1.py")
QEMU = load("minic_cache_qemu_test", "linux-three-isa-gnu-qemu-v1.py")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)
    return path


def fixture(root, target):
    work = root / target / "gnu-work"
    out, ev, frozen = work / "build", work / "evidence", work / "frozen"
    src = work / "linux-6.6.143"
    src.mkdir(parents=True)
    write(src / "Makefile", "VERSION = 6\nPATCHLEVEL = 6\nSUBLEVEL = 143\n")
    tar = work / "linux-6.6.143.tar.xz"
    tar.write_bytes(b"original synthetic Linux tar bytes")
    write(ev / "linux-source.sha256", f"{digest(tar)}  {tar}\n")
    config = write(out / ".config", "CONFIG_EXPERIMENT=y\n")
    write(ev / "config.sha256", f"{digest(config)}  {config}\n")
    write(out / "vmlinux", "original GNU synthetic linked vmlinux\n")
    write(out / QEMU.TARGETS[target]["image"], "original GNU boot Image\n")
    binary = "gcc" if target == "x86_64" else target + "-linux-gnu-gcc"
    if target == "loongarch64":
        binary += "-13"
    triple = {"x86_64": "x86_64-linux-gnu", "aarch64": "aarch64-linux-gnu",
              "loongarch64": "loongarch64-linux-gnu"}[target]
    gcc = write(root / target / "bin" / binary,
                "#!/bin/sh\ncase \"$1\" in\n" +
                f"  -dumpmachine) echo {triple} ;;\n" +
                "  -dumpfullversion) echo 13.2.0 ;;\n" +
                "  *) exit 3 ;;\nesac\n")
    gcc.chmod(0o755)
    arch = {"x86_64": "x86", "aarch64": "arm64", "loongarch64": "loongarch"}[target]
    write(ev / "build-contract.txt",
          f"source=linux-6.6.143\ntarget={target}\narch={arch}\n" +
          f"gnu_cc={binary}\nimage=vmlinux\nprofile=broad\n" +
          "verdict=GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC\n")
    owners = ("arch/test/a.o", "lib/b.o")
    abi_opt = " -mabi=lp64s" if target == "loongarch64" else ""
    rows = []
    for index, name in enumerate(owners):
        obj = out / name
        obj.parent.mkdir(parents=True, exist_ok=True)
        elf = bytearray(64)
        elf[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<H", elf, 16, 1)
        struct.pack_into("<H", elf, 18, GOLDEN.MACHINES[target])
        if target == "loongarch64":
            struct.pack_into("<I", elf, 48, 1)
        obj.write_bytes(elf + b"GNU ORIGINAL")
        source = src / name.replace(".o", ".c")
        write(source, "int original(void) { return 3; }\n")
        cmd = out / Path(name).parent / ("." + Path(name).name + ".cmd")
        write(cmd, f"savedcmd_{name} := {binary}{abi_opt} -c -o {name} {source}\n"
                   f"source_{name} := {source}\n")
        i_path = frozen / "i" / name.replace(".o", ".i")
        write(i_path, "int original(void) { return 3; }\n")
        rows.append(dict(object=name, index=index, shard=index % 7,
                         input="i/" + name.replace(".o", ".i"),
                         sha256=digest(i_path), cmd_sha256=digest(cmd),
                         minic_flags=(["-mabi=lp64s"] if abi_opt else []),
                         gnu_as_flags=(["-mabi=lp64s"] if abi_opt else []),
                         gnu_elf_abi=(1 if abi_opt else None), gnu_output=name))
    GOLDEN.census(out, target, "vmlinux", ev, 2, "broad")
    write(frozen / "manifest.json",
          json.dumps(dict(schema="three-isa-gnu-i-v1",
                          identity=FREEZE.identity(ev, target, str(gcc)), rows=rows)) + "\n")
    return work, str(gcc)


class RestoredGoldenCache(unittest.TestCase):
    def test_three_targets_and_mutations(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for target in GOLDEN.MACHINES:
                with self.subTest(target=target):
                    work, gcc = fixture(root, target)
                    original = AUDIT.verify_cache(target, work, gcc,
                        expected_source_sha=digest(work / "linux-6.6.143.tar.xz"), minimum=2)
                    self.assertEqual(original["owners"], 2)
                    self.assertFalse(original["qemu_receipt_checked"])
                    obj = work / "build/lib/b.o"
                    obj.write_bytes(obj.read_bytes() + b"TAMPER")
                    with self.assertRaisesRegex(ValueError, "drift|hash"):
                        AUDIT.verify_cache(target, work, gcc,
                            expected_source_sha=digest(work / "linux-6.6.143.tar.xz"), minimum=2)
                    obj.write_bytes(obj.read_bytes()[:-6])

                    cmd = work / "build/lib/.b.o.cmd"
                    old_cmd = cmd.read_text()
                    cmd.write_text(old_cmd.replace(" -c ", " -fPIC -c "))
                    with self.assertRaisesRegex(ValueError, "cmd provenance changed"):
                        AUDIT.verify_cache(target, work, gcc,
                            expected_source_sha=digest(work / "linux-6.6.143.tar.xz"), minimum=2)
                    cmd.write_text(old_cmd)

                    src = work / "frozen/i/lib/b.i"
                    old_i = src.read_bytes()
                    src.write_bytes(b"TAMPER\n")
                    with self.assertRaisesRegex(ValueError, "wrong cached .i SHA"):
                        AUDIT.verify_cache(target, work, gcc,
                            expected_source_sha=digest(work / "linux-6.6.143.tar.xz"), minimum=2)
                    src.write_bytes(old_i)

                    config = work / "build/.config"
                    config.write_text("CONFIG_MUTATED=y\n")
                    with self.assertRaisesRegex(ValueError, "source/config SHA"):
                        AUDIT.verify_cache(target, work, gcc,
                            expected_source_sha=digest(work / "linux-6.6.143.tar.xz"), minimum=2)
                    config.write_text("CONFIG_EXPERIMENT=y\n")

                    orphan = work / "build/lib/new.o"
                    orphan.write_bytes(obj.read_bytes())
                    with self.assertRaisesRegex(ValueError, "drift"):
                        AUDIT.verify_cache(target, work, gcc,
                            expected_source_sha=digest(work / "linux-6.6.143.tar.xz"), minimum=2)
                    orphan.unlink()

                    # GNU QEMU output is produced in a distinct RUNTIME_OUTPUT,
                    # not within the independently restored GNU_WORK cache.
                    runtime = root / target / "runtime-output" / "boot"
                    initramfs = write(runtime / "initramfs.cpio.gz", "GNU initramfs bytes")
                    log_common = ("Linux version 6.6.143\n" +
                                  "Run /init as init process\n" +
                                  "MINIC_LINUX_RUNTIME_INIT_BEGIN\n" +
                                  "\n".join(QEMU.P0_MARKERS) + "\n")
                    lanes = []
                    for name in ("p0", "p1"):
                        cmdline = ("Kernel command line: console=" +
                                   QEMU.TARGETS[target]["console"] + " rdinit=/init" +
                                   (" minic_runtime=p1" if name == "p1" else ""))
                        log = log_common + cmdline + "\n"
                        if name == "p1":
                            log += ("RUNTIME_V2_SYSCALL_BEGIN\n" +
                                    "\n".join("RUNTIME_V2_SYSCALL_PASS " + x
                                              for x in QEMU.P1_SUCCESS_EVENTS) + "\n" +
                                    "RUNTIME_V2_SYSCALL_END pass=12 fail=0\n" +
                                    "INIT_RUNTIME_PROBE=PASS\n")
                        logfile = write(runtime / (name + ".log"), log)
                        lanes.append({"lane": name, "log": str(logfile),
                                      "log_sha256": digest(logfile),
                                      "exit_code": 0, "timed_out": False,
                                      "verdict": "GNU_QEMU_RUNTIME_PASS"})
                    gnu_receipt = runtime / "gnu-qemu-receipt.json"
                    write(gnu_receipt, json.dumps({
                        "schema": "minic-three-isa-gnu-qemu-v1", "lanes": lanes,
                        "verdict": "GNU_QEMU_P0_P1_PASS_NOT_MINIC", "target": target,
                        "source_sha256": original["source_sha256"],
                        "config_sha256": original["config_sha256"],
                        "gnu_c_owners": 2,
                        "gnu_c_owner_manifest_sha256": original["owner_manifest_sha256"],
                        "gnu_linked_kernel_sha256": original["kernel_sha256"],
                        "boot_image": str(work / "build" / QEMU.TARGETS[target]["image"]),
                        "image_sha256": digest(work / "build" / QEMU.TARGETS[target]["image"]),
                        "initramfs": str(initramfs),
                        "initramfs_sha256": digest(initramfs),
                    }) + "\n")
                    self.assertTrue(AUDIT.verify_cache(target, work, gcc, gnu_receipt,
                        expected_source_sha=original["source_sha256"], minimum=2)["qemu_receipt_checked"])
                    gnu_receipt.write_text(gnu_receipt.read_text().replace(
                        "GNU_QEMU_P0_P1_PASS_NOT_MINIC", "GNU_QEMU_FAIL"))
                    with self.assertRaisesRegex(ValueError, "receipt"):
                        AUDIT.verify_cache(target, work, gcc, gnu_receipt,
                            expected_source_sha=original["source_sha256"], minimum=2)
                    gnu_receipt.write_text(gnu_receipt.read_text().replace(
                        "GNU_QEMU_FAIL", "GNU_QEMU_P0_P1_PASS_NOT_MINIC"))
                    with (runtime / "p1.log").open("a") as stream:
                        stream.write("TAMPER\n")
                    with self.assertRaisesRegex(ValueError, "lane receipt"):
                        AUDIT.verify_cache(target, work, gcc, gnu_receipt,
                            expected_source_sha=original["source_sha256"], minimum=2)

    def test_symlinks_and_foreign_o_path(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            work, gcc = fixture(root, "x86_64")
            tar_sha = digest(work / "linux-6.6.143.tar.xz")
            cmd = work / "build/lib/.b.o.cmd"
            original = cmd.read_text()
            cmd.write_text(original.replace(" -o lib/b.o", " -o /outside/old-build/lib/b.o"))
            # Even if an attacker signs a new frozen row, O= must not escape.
            rows_file = work / "frozen/manifest.json"
            rows = json.loads(rows_file.read_text())
            rows["rows"][1]["cmd_sha256"] = digest(cmd)
            rows["rows"][1]["gnu_output"] = "/outside/old-build/lib/b.o"
            rows_file.write_text(json.dumps(rows) + "\n")
            with self.assertRaisesRegex(ValueError, "stale/foreign.*O=|escaped Kbuild"):
                AUDIT.verify_cache("x86_64", work, gcc,
                    expected_source_sha=tar_sha, minimum=2)
            cmd.write_text(original)
            rows["rows"][1]["cmd_sha256"] = digest(cmd)
            rows["rows"][1]["gnu_output"] = "lib/b.o"
            rows_file.write_text(json.dumps(rows) + "\n")
            obj = work / "build/lib/b.o"
            copy = work / "build/lib/b-copy.o"
            shutil.copyfile(obj, copy)
            obj.unlink()
            obj.symlink_to(copy)
            with self.assertRaisesRegex(ValueError, "symlink"):
                AUDIT.verify_cache("x86_64", work, gcc,
                    expected_source_sha=tar_sha, minimum=2)


if __name__ == "__main__":
    unittest.main()
