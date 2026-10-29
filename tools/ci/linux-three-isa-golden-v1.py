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

# Non-main ELF domains are real Kbuild products, not 64-bit MiniC owners.
# Keep 32-bit ABIs explicit (including any C owners) instead of dropping them.
COMPAT_DOMAINS = {
    "x86_64": (
        ("arch/x86/boot/", 3),
        ("arch/x86/realmode/", 3),
        ("arch/x86/entry/vdso/vdso32/", 3),
    ),
    "aarch64": (("arch/arm64/kernel/vdso32/", 40),),
}

def classify_object(path: Path, out: Path, target: str) -> str:
    if path.is_symlink():
        raise ValueError("symlink object is not an acceptable golden: " + str(path))
    with path.open("rb") as stream:
        hdr = stream.read(64)
    if hdr[:4] != b"\x7fELF":
        return "non-elf"
    if len(hdr) < 20:
        raise ValueError("truncated GNU ELF object: " + str(path))
    elf_class, endian = hdr[4], hdr[5]
    if endian != 1 or elf_class not in (1, 2):
        raise ValueError("unexpected target ELF class/data: " + str(path))
    if len(hdr) < (52 if elf_class == 1 else 64):
        raise ValueError("truncated GNU ELF header: " + str(path))
    kind = struct.unpack_from("<H", hdr, 16)[0]
    machine = struct.unpack_from("<H", hdr, 18)[0]
    if kind != 1:
        return "non-relocatable"
    if elf_class == 2:
        if machine != MACHINES[target]:
            return "foreign-elf64"
        return "main"
    rel = path.relative_to(out).as_posix()
    # x32 rebrands x86-64 vDSO object code into ELF32/EM_X86_64
    # using objcopy; it is a separate ABI but not an additional C owner.
    if (target == "x86_64" and rel.startswith("arch/x86/entry/vdso/")
            and rel.endswith("-x32.o")):
        if machine != MACHINES["x86_64"]:
            raise ValueError("unexpected x32 ELF target machine: " + str(path))
        return "compat"
    for prefix, expected_machine in COMPAT_DOMAINS.get(target, ()):
        if rel.startswith(prefix):
            if machine != expected_machine:
                raise ValueError("unexpected compat ELF32 target machine: " + str(path))
            return "compat"
    raise ValueError("unclassified target ELF32 relocatable: " + str(path))

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
    classified = [
        (obj, classify_object(obj, out, target))
        for obj in candidates if not is_host_object(obj)
    ]
    objects = [obj for obj, domain in classified if domain == "main"]
    compat = [obj for obj, domain in classified if domain == "compat"]
    foreign = [obj for obj, domain in classified if domain == "foreign-elf64"]
    if not objects:
        raise ValueError("no matching GNU Linux target ELF relocatables")
    # Retain original x86 early-boot domain count for historical comparisons.
    boot32 = [
        obj.relative_to(out).as_posix() for obj in compat
        if target == "x86_64"
        and obj.relative_to(out).as_posix().startswith(
            ("arch/x86/boot/", "arch/x86/realmode/"))
    ]
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / "separate-host-build-objects.txt").write_text(
        "# GNU Kbuild host helper objects, never MiniC kernel C owners\n"
        + "".join(o.relative_to(out).as_posix() + "\n" for o in host_objects))
    (evidence / "separate-x86-elf32-boot-domain.txt").write_text(
        "# x86 early boot ELF32; not x86_64 MiniC certified C owners\n"
        + "".join(name + "\n" for name in boot32))
    (evidence / "separate-foreign-elf64-objects.sha256").write_text("".join(
        f"{sha(obj)}  {obj.relative_to(out).as_posix()}\n" for obj in foreign
    ))
    golden = evidence / "gnu-target-objects.sha256"
    golden.write_text("".join(
        f"{sha(obj)}  {obj.relative_to(out).as_posix()}\n" for obj in objects
    ))
    # RV32/RV64 precedent: ABI-separated objects get their own SHA-locked
    # universe and the same canonical GNU .o.cmd C-owner verifier.
    compat_golden = evidence / "gnu-compat-elf32-objects.sha256"
    compat_golden.write_text("".join(
        f"{sha(obj)}  {obj.relative_to(out).as_posix()}\n" for obj in compat
    ))
    common = full_c_module()
    owners, excluded = common.examine(out, golden)
    compat_owners, compat_excluded = common.examine(out, compat_golden)
    compat_c_set = set(compat_owners)
    if len(compat_owners) != len(compat_c_set):
        raise ValueError("duplicate 32-bit compat C owners")
    (evidence / "separate-elf32-compat-owners.txt").write_text(
        "# ABI-separate ELF32 C owners; not certified by the ELF64 MiniC run\n"
        + "".join(name + "\n" for name in sorted(compat_owners)))
    (evidence / "separate-elf32-compat-objects.tsv").write_text(
        "object\tsha256\tsource_kind\n"
        + "".join(
            f"{obj.relative_to(out).as_posix()}\t{sha(obj)}\t"
            f"{'C' if obj.relative_to(out).as_posix() in compat_c_set else 'non-C'}\n"
            for obj in compat))
    (evidence / "separate-elf32-compat-excluded.tsv").write_text(
        "object\treason\n" +
        "".join(f"{name}\t{reason}\n" for name, reason in compat_excluded))
    if len(owners) < minimum or len(owners) != len(set(owners)):
        raise ValueError("incomplete or duplicate original Kbuild C-owner universe: "
                         + str(len(owners)))
    config = out / ".config"
    if not config.is_file():
        raise ValueError("missing frozen GNU Kconfig")
    # Record the resolved default compatibility settings rather than forcing
    # extra ABI builds whose C translation units MiniC does not yet target.
    configured = set(config.read_text().splitlines())
    compat_symbols = ("CONFIG_IA32_EMULATION", "CONFIG_X86_X32_ABI",
                      "CONFIG_COMPAT", "CONFIG_COMPAT_VDSO")
    effective_compat_kconfig = {
        symbol: ("y" if f"{symbol}=y" in configured else
                 "m" if f"{symbol}=m" in configured else "n")
        for symbol in compat_symbols
    }
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
        "effective_compat_kconfig": effective_compat_kconfig,
        "gnu_target_objects": len(objects), "gnu_c_owners": len(owners),
        "excluded_non_c_objects": len(excluded),
        "separate_host_build_objects": len(host_objects),
        "separate_foreign_elf64_objects": len(foreign),
        "separate_host_manifest_sha256": sha(evidence / "separate-host-build-objects.txt"),
        "separate_x86_early_boot_elf32_objects": len(boot32),
        "separate_elf32_compat_objects": len(compat),
        "separate_elf32_compat_c_owners": len(compat_owners),
        "separate_elf32_compat_manifest_sha256": sha(compat_golden),
        "separate_elf32_compat_c_manifest_sha256": sha(evidence / "separate-elf32-compat-owners.txt"),
        "scope": "ELF64_MAIN_TARGET_C_ONLY_ELF32_ABI_SEPARATELY_ATTESTED",
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
            compat_cases = {
                "x86_64": (
                    ("arch/x86/boot/regs.o", 3, ".S"),
                    ("arch/x86/realmode/rm/wakeup_asm.o", 3, ".S"),
                    ("arch/x86/entry/vdso/vdso32/note.o", 3, ".S"),
                    ("arch/x86/entry/vdso/vdso32/vclock_gettime.o", 3, ".c"),
                    ("arch/x86/entry/vdso/vclock_gettime-x32.o", 62, ".S"),
                ),
                "aarch64": (
                    ("arch/arm64/kernel/vdso32/vgettimeofday.o", 40, ".c"),
                ),
            }
            for name, compat_machine, suffix in compat_cases.get(target, ()):
                obj = out / name
                obj.parent.mkdir(parents=True, exist_ok=True)
                hdr = bytearray(64)
                hdr[0:6] = b"\x7fELF\x01\x01"
                struct.pack_into("<H", hdr, 16, 1)
                struct.pack_into("<H", hdr, 18, compat_machine)
                obj.write_bytes(hdr)
                (obj.parent / ("." + obj.name + ".cmd")).write_text(
                    f"source_{name} := /fixed/original/{name[:-2]}{suffix}\n")
            ev = base / target / "evidence"
            result = census(out, target, "vmlinux", ev, minimum=2)
            assert result["separate_x86_early_boot_elf32_objects"] == (
                2 if target == "x86_64" else 0)
            assert result["separate_elf32_compat_objects"] == {
                "x86_64": 5, "aarch64": 1, "loongarch64": 0
            }[target]
            assert result["separate_elf32_compat_c_owners"] == {
                "x86_64": 1, "aarch64": 1, "loongarch64": 0
            }[target]
            assert result["gnu_c_owners"] == 2
            assert result["gnu_target_objects"] == 2
            assert result["separate_host_build_objects"] == 3
            assert result["verdict"] == "GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC"
            # Neither a foreign ELF32 ABI nor an undisclosed ELF32 build
            # scope may silently disappear from the MiniC coverage denominator.
            unexpected = out / "lib/unexpected32.o"
            bad_hdr = bytearray(64)
            bad_hdr[0:6] = b"\x7fELF\x01\x01"
            struct.pack_into("<H", bad_hdr, 16, 1)
            struct.pack_into("<H", bad_hdr, 18, 3)
            unexpected.write_bytes(bad_hdr)
            try:
                classify_object(unexpected, out, target)
            except ValueError as exc:
                assert "unclassified target ELF32" in str(exc)
            else:
                raise AssertionError("accepted unclassified ELF32")
            unexpected.unlink()
            if target == "x86_64":
                wrong = out / "arch/x86/entry/vdso/vdso32/wrong.o"
                wrong.write_bytes(bad_hdr)
                wrong_hdr = bytearray(bad_hdr)
                struct.pack_into("<H", wrong_hdr, 18, 40)
                wrong.write_bytes(wrong_hdr)
                try:
                    classify_object(wrong, out, target)
                except ValueError as exc:
                    assert "unexpected compat ELF32 target machine" in str(exc)
                else:
                    raise AssertionError("accepted foreign compat ELF32 machine")
                wrong.unlink()
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
              "host_excluded=3 compat_elf32_domains=3 compat_C_isolated=1 "
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
