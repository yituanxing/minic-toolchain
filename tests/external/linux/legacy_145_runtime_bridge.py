#!/usr/bin/env python3
"""Reuse the archived Python-MiniC 145-module/203-function QEMU oracle.

The historical cpio is external input, never silently downloaded or modified.
The same archive must be supplied to a pure-GCC reference run and a MiniC run.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ORIGINAL_SHA256 = "88c81f1b9eaa3377fbed3f99f29d34c60491eede6ccd57966a609b16b619a51b"
ENDPOINT = "M38_FINAL145_V028_END pass=203 fail=0"
POWERDOWN = "reboot: Power down"
# Authenticated from the original V024 145/203 QEMU log. Each ID occurs
# exactly once; compare sorted IDs, not just counts or success phrases.
REFERENCE_MODULE_IDS_SHA256 = "a0bf40902063254aec9203600f7b75c54bbacb5106a14dcaca3d31454e6c3b59"
REFERENCE_FUNCTION_IDS_SHA256 = "c1105105a3280ab0d4bbec09e08f57dccc870318791b0fd14b3e6cad86a04a19"

def ids_digest(markers):
    identifiers = [line.split(None, 1)[0] for line in markers]
    return sha256(("\n".join(sorted(identifiers)) + "\n").encode()), len(set(identifiers))
BAD_KERNEL = re.compile(
    r"Kernel panic|Oops:|BUG:|soft lockup|hard LOCKUP|workqueue lockup|"
    r"Unable to handle kernel|Kernel stack overflow", re.I
)
MODULE_COMPAT = re.compile(
    r"Unknown symbol|invalid module format|disagrees about version of symbol|"
    r"module verification failed|version magic|vermagic", re.I
)
MODULE_PASS = re.compile(r"^MODULE_PASS\s+(.+?)\s*$", re.M)
FUNCTION_PASS = re.compile(r"^FUNCTION_PASS\s+(.+?)\s*$", re.M)
MODULE_FAIL = re.compile(r"^MODULE_FAIL\b.*$", re.M)
FUNCTION_FAIL = re.compile(r"^FUNCTION_FAIL\b.*$", re.M)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def cpio_newc_entries(raw):
    """Read Linux newc archives without unpacking untrusted paths onto disk."""
    position = 0
    while position + 110 <= len(raw):
        header = raw[position:position + 110]
        if header[:6] not in (b"070701", b"070702"):
            raise ValueError("unsupported or malformed cpio header at byte %d" % position)
        length = int(header[54:62], 16)
        namesize = int(header[94:102], 16)
        name_end = position + 110 + namesize
        name = raw[position + 110:name_end - 1].decode("utf-8", "surrogateescape")
        offset = (name_end + 3) & ~3
        end = offset + length
        if namesize < 1 or end > len(raw):
            raise ValueError("truncated cpio entry: " + name)
        if name == "TRAILER!!!":
            break
        yield name, memoryview(raw)[offset:end]
        position = (end + 3) & ~3


def inspect_archive(archive, allow_noncanonical):
    raw = archive.read_bytes()
    actual = sha256(raw)
    if actual != ORIGINAL_SHA256 and not allow_noncanonical:
        raise ValueError("historical archive SHA mismatch: " + actual)
    entries = dict(cpio_newc_entries(raw))
    modules = sorted(name for name in entries if name.endswith(".ko"))
    init = entries.get("init")
    if init is None or len(init) < 64 or init[:4].tobytes() != b"\x7fELF":
        raise ValueError("historical /init is missing or is not ELF")
    if struct.unpack_from("<H", init, 18)[0] != 243:
        raise ValueError("historical /init is not RISC-V ELF")
    if len(modules) != 145:
        raise ValueError("expected exactly 145 modules; found %d" % len(modules))
    init_bytes = init.tobytes()
    if ENDPOINT.encode() not in init_bytes and b"M38_FINAL145_V028_END pass=%d fail=%d" not in init_bytes:
        raise ValueError("historical /init has no 145/203 gate endpoint")
    vermagics = {}
    for name in modules:
        match = re.search(rb"vermagic=([^\x00]+)", entries[name])
        if not match:
            raise ValueError("missing module vermagic: " + name)
        v = match.group(1).decode("ascii", "replace")
        vermagics[v] = vermagics.get(v, 0) + 1
    return {
        "archive_sha256": actual,
        "archive_bytes": len(raw),
        "legacy_init_sha256": sha256(init_bytes),
        "legacy_init_bytes": len(init_bytes),
        "module_count": len(modules),
        "module_vermagic": vermagics,
        "sample_modules": [
            p for p in modules if p.endswith((
                "/overlay.ko", "/bridge.ko", "/nf_conntrack.ko",
                "/btrfs.ko", "/kvm.ko", "/nouveau.ko", "/radeon.ko"
            ))
        ],
        "sha_verified": actual == ORIGINAL_SHA256,
    }


def classify_log(content, returncode=None, timeout=False):
    log = content.replace("\r\n", "\n")
    modules = MODULE_PASS.findall(log)
    functions = FUNCTION_PASS.findall(log)
    module_failures = MODULE_FAIL.findall(log)
    function_failures = FUNCTION_FAIL.findall(log)
    module_ids_hash, unique_modules = ids_digest(modules)
    function_ids_hash, unique_functions = ids_digest(functions)
    module_id_set_match = (
        unique_modules == 145
        and module_ids_hash == REFERENCE_MODULE_IDS_SHA256
    )
    function_id_set_match = (
        unique_functions == 203
        and function_ids_hash == REFERENCE_FUNCTION_IDS_SHA256
    )
    kernel_faults = [line for line in log.splitlines() if BAD_KERNEL.search(line)]
    compatibility = [line for line in log.splitlines() if MODULE_COMPAT.search(line)]
    endpoint = ENDPOINT in log
    powerdown = POWERDOWN in log
    passed = (endpoint and powerdown and len(modules) == 145
              and len(functions) == 203
              and module_id_set_match and function_id_set_match
              and not module_failures
              and not function_failures and not kernel_faults
              and not compatibility and not timeout
              and returncode in (None, 0))
    if passed:
        verdict = "PASS"
    elif compatibility:
        verdict = "ENVIRONMENT_OR_MODULE_ABI_MISMATCH"
    elif kernel_faults or module_failures or function_failures:
        verdict = "FAIL_RUNTIME"
    else:
        verdict = "INCONCLUSIVE"
    return {
        "verdict": verdict,
        "module_pass": len(modules),
        "function_pass": len(functions),
        "distinct_module_ids": unique_modules,
        "distinct_function_ids": unique_functions,
        "module_id_set_match": module_id_set_match,
        "function_id_set_match": function_id_set_match,
        "module_id_set_sha256": module_ids_hash,
        "function_id_set_sha256": function_ids_hash,
        "module_fail": len(module_failures),
        "function_fail": len(function_failures),
        "endpoint": endpoint,
        "normal_powerdown": powerdown,
        "qemu_rc": returncode,
        "qemu_timeout": timeout,
        "last_module_pass": modules[-1] if modules else None,
        "last_function_pass": functions[-1] if functions else None,
        "first_module_failure": module_failures[0] if module_failures else None,
        "first_function_failure": function_failures[0] if function_failures else None,
        "first_kernel_fault": kernel_faults[0] if kernel_faults else None,
        "first_module_compatibility_error": compatibility[0] if compatibility else None,
    }


def run_image(args, archive, output):
    root = Path(__file__).resolve().parents[3]
    wrapper = root / "tests/external/linux/qemu_explicit_initrd_wrapper.py"
    qemu = shutil.which(args.qemu)
    if qemu is None:
        raise ValueError("missing QEMU: " + args.qemu)
    if not shutil.which("fdtget") or not shutil.which("fdtput"):
        raise ValueError("missing fdtget/fdtput (install device-tree-compiler)")
    image = args.image.resolve()
    if not image.is_file():
        raise ValueError("missing kernel Image: " + str(image))
    output.mkdir(parents=True, exist_ok=True)
    cmdline = (
        "console=ttyS0 earlycon=uart8250,mmio,0x10000000,115200n8 "
        "loglevel=8 rdinit=/init nowatchdog workqueue.watchdog_thresh=0 "
        "hung_task_timeout_secs=0 panic=-1"
    )
    command = [
        sys.executable, str(wrapper), "-M", "virt", "-cpu", "max",
        "-m", str(args.memory) + "M", "-smp", "1", "-nographic",
        "-no-reboot", "-monitor", "none", "-bios", "default",
        "-kernel", str(image), "-initrd", str(archive),
        "-append", cmdline,
    ]
    environment = os.environ.copy()
    environment["QEMU_REAL_SYSTEM_RISCV64"] = qemu
    environment["QEMU_EXPLICIT_DTB_DIR"] = str(output / "dtb")
    timed_out = False
    log_file = output / "qemu.log"
    with log_file.open("wb") as capture:
        try:
            run = subprocess.run(command, env=environment, stdout=capture,
                                 stderr=subprocess.STDOUT, timeout=args.timeout,
                                 check=False)
            rc = run.returncode
        except subprocess.TimeoutExpired:
            timed_out, rc = True, 124
    result = classify_log(log_file.read_text(errors="replace"), rc, timed_out)
    result["image_sha256"] = sha256(image.read_bytes())
    result["cmdline"] = cmdline
    result["memory_mib"] = args.memory
    result["log_path"] = str(log_file)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True,
                        help="original natural145-v024-cold-overlay.cpio")
    parser.add_argument("--image", type=Path,
                        help="exact-config GCC or MiniC kernel Image to boot")
    parser.add_argument("--historical-log", type=Path,
                        help="validate classifier against a preserved historical log")
    parser.add_argument("--output-dir", type=Path, default=Path("build/legacy145"))
    parser.add_argument("--label", default="reference")
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--memory", type=int, default=1024)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--allow-noncanonical-archive", action="store_true",
                        help="only for explicit experimentation; record original SHA")
    args = parser.parse_args()
    if args.image and args.historical_log:
        parser.error("choose an Image run OR a preserved log, not both")
    if args.timeout < 1 or args.memory < 512:
        parser.error("timeout must be positive; memory must be at least 512 MiB")
    try:
        archive = args.archive.resolve()
        report = {"archive": inspect_archive(archive, args.allow_noncanonical_archive)}
        if args.historical_log:
            report["result"] = classify_log(
                args.historical_log.read_text(errors="replace"), returncode=0)
        elif args.image:
            report["result"] = run_image(args, archive, args.output_dir / args.label)
        args.output_dir.mkdir(parents=True, exist_ok=True)
        destination = args.output_dir / (args.label + "-legacy145.json")
        destination.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
        if "result" in report and report["result"]["verdict"] != "PASS":
            return 1
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as err:
        print("LEGACY145_ERROR: " + str(err), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
