#!/usr/bin/env python3
"""One GNU Linux C-owner census for all target ISAs; BUILD-ONLY evidence.

Reuses the existing full-C .o.cmd verifier; the only target cells are ELF
machine and boot/image path in the calling GNU Kbuild profile. QEMU is NOT
claimed here. No GNU C fallback ever counts as MiniC acceptance.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile

MACHINES = {"x86_64": 62, "aarch64": 183, "loongarch64": 258}

def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()

def full_c_module():
    path = Path(__file__).with_name("linux-runtime-gcc-full-c-objects-v1.py")
    spec = importlib.util.spec_from_file_location("minic_gnu_full_c", path)
    if spec is None or spec.loader is None:
        raise ValueError("missing canonical Linux GNU C-owner verifier")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def target_object(path: Path, expected_machine: int) -> bool:
    if path.is_symlink():
        raise ValueError("symlink object is not an acceptable golden: " + str(path))
    with path.open("rb") as stream:
        hdr = stream.read(64)
    if hdr[:4] != b"\x7fELF":
        return False
    if len(hdr) < 52:
        raise ValueError("truncated GNU ELF object: " + str(path))
    # x86 bzImage legitimately contains a separate 32-bit early-boot domain.
    # Do not classify its .o as a 64-bit MiniC target C owner.
    if hdr[4:6] != b"\x02\x01":
        if (expected_machine == MACHINES["x86_64"]
                and hdr[4:6] == b"\x01\x01"
                and ("/arch/x86/boot/" in path.as_posix()
                     or "/arch/x86/realmode/" in path.as_posix())):
            return False
        raise ValueError("unexpected target ELF class/data: " + str(path))
    machine = struct.unpack_from("<H", hdr, 18)[0]
    kind = struct.unpack_from("<H", hdr, 16)[0]
    return kind == 1 and machine == expected_machine

def census(out: Path, target: str, image: str, evidence: Path, minimum: int,
           config_profile: str = "broad") -> dict:
    if target not in MACHINES or minimum < 1 or config_profile not in ("common", "broad"):
        raise ValueError("unsupported target or invalid minimum")
    out = out.resolve(strict=True)
    path = Path(image)
    if path.is_absolute() or ".." in path.parts or not image:
        raise ValueError("kernel image must be a relative path below GNU build tree")
    kernel = out / path
    if not kernel.is_file() or kernel.stat().st_size == 0:
        raise ValueError("missing actual GNU-linked kernel: " + str(kernel))
    # The host compiler has the same ELF e_machine as the x86_64 target.
    # ELF class/machine alone cannot identify Linux *target* C owners.
    # Kbuild's tools/ and scripts/ object domains are host executables'
    # build products; arch/*/tools likewise contains host build helpers.
    # Keep these visible in a separate attested list, not silently counted
    # among target MiniC C translation units.
    def is_host_object(obj: Path) -> bool:
        parts = obj.relative_to(out).parts
        return (bool(parts) and parts[0] in ("tools", "scripts")
                or len(parts) >= 3 and parts[0] == "arch" and
                parts[2] == "tools")
    candidates = sorted(o for o in out.rglob("*.o") if o.is_file())
    host_objects = [o for o in candidates if is_host_object(o)]
    objects = [
        o for o in candidates if not is_host_object(o)
        and target_object(o, MACHINES[target])
    ]
    if not objects:
        raise ValueError("no matching GNU Linux target ELF relocatables")
    # Preserve excluded ELF32 boot objects as a separate explicit scope.
    boot32 = []
    if target == "x86_64":
        for domain in ("arch/x86/boot", "arch/x86/realmode"):
            for o in sorted((out / domain).rglob("*.o")):
                if o.is_file() and not o.is_symlink():
                    with o.open("rb") as stream:
                        magic = stream.read(6)
                    if magic == b"\x7fELF\x01\x01":
                        boot32.append(o.relative_to(out).as_posix())
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / "separate-host-build-objects.txt").write_text(
        "# GNU Kbuild host helper objects, never MiniC kernel C owners\n"
        + "".join(o.relative_to(out).as_posix() + "\n" for o in host_objects))
    (evidence / "separate-x86-elf32-boot-domain.txt").write_text(
        "# x86 early boot ELF32; not x86_64 MiniC certified C owners\n"
        + "".join(name + "\n" for name in boot32))
    golden = evidence / "gnu-target-objects.sha256"
    golden.write_text("".join(
        f"{sha(obj)}  {obj.relative_to(out).as_posix()}\n" for obj in objects
    ))
    common = full_c_module()
    owners, excluded = common.examine(out, golden)
    if len(owners) < minimum or len(owners) != len(set(owners)):
        raise ValueError("incomplete or duplicate original Kbuild C-owner universe: "
                         + str(len(owners)))
    config = out / ".config"
    if not config.is_file():
        raise ValueError("missing frozen GNU Kconfig")
    # The canonical C owner set is derived from true .o.cmd metadata, NOT
    # from source-name guessing or an RV64 fixed 2064-object assumption.
    owners = sorted(owners)
    (evidence / "full-c-owners.txt").write_text(
        "# GNU Kbuild C owners; not MiniC success or QEMU boot evidence\n"
        + "\n".join(owners) + "\n")
    (evidence / "excluded.tsv").write_text(
        "object\treason\n"
        + "".join(f"{obj}\t{reason}\n" for obj, reason in excluded))
    report = {
        "schema": "minic-linux-three-isa-gnu-build-v1",
        "verdict": "GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC",
        "target": target, "elf_machine": MACHINES[target],
        "config_profile": config_profile,
        "kernel": image, "kernel_sha256": sha(kernel),
        "config_sha256": sha(config),
        "gnu_target_objects": len(objects), "gnu_c_owners": len(owners),
        "excluded_non_c_objects": len(excluded),
        "separate_host_build_objects": len(host_objects),
        "separate_host_manifest_sha256": sha(evidence / "separate-host-build-objects.txt"),
        "separate_x86_early_boot_elf32_objects": len(boot32),
        "scope": "ELF64_MAIN_TARGET_C_ONLY_NOT_X86_EARLY_BOOT_ELF32",
        "owner_manifest_sha256": sha(evidence / "full-c-owners.txt"),
    }
    (evidence / "discovery.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"MINIC_LINUX_THREE_GNU_BUILD=PASS target={target} "
          f"original_C_owners={len(owners)} GNU_objects={len(objects)} "
          "QEMU_certified=0 MiniC_C_owners=0")
    return report

def check_common_config(shared: Path, effective: Path, evidence: Path) -> dict:
    """Compare one Kconfig miniconfig to each target's actual resolved .config."""
    expected = {}
    for raw in shared.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("=", 1)
        if (len(parts) != 2 or not parts[0].startswith("CONFIG_")
                or parts[1] not in ("y", "m", "n") or parts[0] in expected):
            raise ValueError("invalid common Kconfig feature: " + line)
        expected[parts[0]] = parts[1]
    if not expected or expected.get("CONFIG_64BIT") != "y":
        raise ValueError("shared Linux config must anchor 64-bit targets")
    active = {}
    for raw in effective.read_text().splitlines():
        if raw.startswith("CONFIG_") and "=" in raw:
            key, value = raw.split("=", 1)
            active[key] = value
        elif raw.startswith("# CONFIG_") and raw.endswith(" is not set"):
            key = raw[len("# "): -len(" is not set")]
            active[key] = "n"
    rejected = {
        key: {"requested": value, "effective": active.get(key, "n")}
        for key, value in expected.items() if active.get(key, "n") != value
    }
    report = {
        "schema": "minic-three-isa-common-kconfig-v1",
        "profile": "common",
        "shared_sha256": sha(shared),
        "effective_sha256": sha(effective),
        "requested_features": expected,
        "unsatisfied": rejected,
        "parity": "FAIL" if rejected else "PASS",
    }
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / "common-kconfig-contract.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n")
    if rejected:
        raise ValueError("cross-ISA common Kconfig feature mismatch: " +
                         ", ".join(rejected))
    print("MINIC_LINUX_COMMON_KCONFIG=PASS features=" + str(len(expected)) +
          " requested_sha256=" + report["shared_sha256"] +
          " effective_sha256=" + report["effective_sha256"])
    return report


def self_test() -> None:
    with tempfile.TemporaryDirectory() as temp:
        base = Path(temp)
        for target, machine in MACHINES.items():
            out = base / target / "out"
            out.mkdir(parents=True)
            (out / ".config").write_text("CONFIG_TEST=y\n")
            (out / "vmlinux").write_bytes(b"synthetic GNU-only test")
            for name in ("arch/test/a.o", "lib/b.o"):
                p = out / name
                p.parent.mkdir(parents=True, exist_ok=True)
                hdr = bytearray(64)
                hdr[0:6] = b"\x7fELF\x02\x01"
                struct.pack_into("<H", hdr, 16, 1)
                struct.pack_into("<H", hdr, 18, machine)
                p.write_bytes(hdr)
                (p.parent / ("." + p.name + ".cmd")).write_text(
                    f"source_{name} := /fixed/original/{name[:-2]}.c\n")
            # Host objects must never pollute another ISA's source list.
            host = out / "scripts/host.o"
            host.parent.mkdir(parents=True)
            hdr = bytearray(64)
            hdr[0:6] = b"\x7fELF\x02\x01"
            struct.pack_into("<H", hdr, 16, 1)
            struct.pack_into("<H", hdr, 18, 183 if machine != 183 else 62)
            host.write_bytes(hdr)
            for name in ("tools/objtool/check.o", "arch/test/tools/host.o"):
                helper = out / name
                helper.parent.mkdir(parents=True, exist_ok=True)
                hdr = bytearray(64)
                hdr[0:6] = b"\x7fELF\x02\x01"
                struct.pack_into("<H", hdr, 16, 1)
                struct.pack_into("<H", hdr, 18, machine)
                helper.write_bytes(hdr)
            if target == "x86_64":
                for name in ("arch/x86/boot/regs.o",
                             "arch/x86/realmode/rm/wakeup_asm.o"):
                    boot = out / name
                    boot.parent.mkdir(parents=True)
                    boot.write_bytes(b"\x7fELF\x01\x01" + bytes(64))
            ev = base / target / "evidence"
            result = census(out, target, "vmlinux", ev, minimum=2)
            assert result["separate_x86_early_boot_elf32_objects"] == (
                2 if target == "x86_64" else 0)
            assert result["gnu_c_owners"] == 2
            assert result["gnu_target_objects"] == 2
            assert result["separate_host_build_objects"] == 3
            assert result["verdict"] == "GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC"
            tamper = out / "lib/b.o"
            tamper.write_bytes(tamper.read_bytes() + b"TAMPER")
            try:
                full_c_module().examine(out, ev / "gnu-target-objects.sha256")
            except ValueError as exc:
                assert "hash mismatch" in str(exc)
            else:
                raise AssertionError("accepted tampered GNU object")
        shared = base / "common.config"
        actual = base / "effective.config"
        shared.write_text("CONFIG_64BIT=y\nCONFIG_BLOCK=y\n")
        actual.write_text("CONFIG_64BIT=y\nCONFIG_BLOCK=y\n")
        assert check_common_config(shared, actual, base / "profile")["parity"] == "PASS"
        actual.write_text("CONFIG_64BIT=y\n# CONFIG_BLOCK is not set\n")
        try:
            check_common_config(shared, actual, base / "profile")
        except ValueError as exc:
            assert "CONFIG_BLOCK" in str(exc)
        else:
            raise AssertionError("accepted missing common Kconfig feature")
        print("MINIC_LINUX_THREE_GNU_SELFTEST=PASS targets=3 "
              "host_excluded=3 early_boot_domains=2 shared_kconfig_fail_closed=1 "
              "sha_tamper_rejected=1 no_false_qemu=1")

def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--verify-common-config", action="store_true")
    p.add_argument("--common-config", type=Path)
    p.add_argument("--effective-config", type=Path)
    p.add_argument("--config-profile", choices=("common", "broad"), default="broad")
    p.add_argument("--out", type=Path)
    p.add_argument("--target", choices=tuple(MACHINES))
    p.add_argument("--image")
    p.add_argument("--evidence", type=Path)
    p.add_argument("--min-c-objects", type=int, default=1000)
    args = p.parse_args()
    try:
        if args.self_test:
            self_test()
        elif args.verify_common_config:
            if args.common_config is None or args.effective_config is None or args.evidence is None:
                p.error("common Kconfig verification requires three path inputs")
            check_common_config(args.common_config, args.effective_config, args.evidence)
        elif (args.out is None or args.target is None
              or args.image is None or args.evidence is None):
            p.error("--out, --target, --image and --evidence required")
        else:
            census(args.out, args.target, args.image, args.evidence,
                   args.min_c_objects, args.config_profile)
        return 0
    except (OSError, ValueError, AssertionError) as exc:
        print(f"MINIC_LINUX_THREE_GNU=FAIL {exc}", file=sys.stderr)
        return 2

if __name__ == "__main__":
    raise SystemExit(main())
