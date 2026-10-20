#!/usr/bin/env python3
"""Full mixed-object pressure test, original BusyBox C/Kconfig/Kbuild unchanged.

Uses independently generated MiniC ELF objects for EVERY successfully
qualified TU of the pinned 957-symbol host-compatible Kconfig. Rebuilds
every original object through the exact upstream Kbuild GCC invocation and
substitutes only that owner's MiniC ELF relocatable object before archive.
Never claims all-MiniC when any owner remains GCC. All outcomes are
diagnostic + fail-closed, with all source files unchanged.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from mixed_original_kbuild import run_cases, sha256


# Exact acceptance from the certified original 957-option 593-object
# BusyBox 1.36.1 Kbuild. A formerly working MiniC owner must not silently
# become an extra GCC fallback just because other objects still link.
CERTIFIED_KBUILD_OWNERS = 593
CERTIFIED_MINIC_OWNERS = 581
CERTIFIED_GCC_FALLBACKS = frozenset((
    "libbb/hash_md5_sha.o",
    "libbb/verror_msg.o",
    "libbb/vfork_daemon_rexec.o",
    "libbb/xconnect.o",
    "miscutils/devfsd.o",
    "miscutils/hdparm.o",
    "networking/ifconfig.o",
    "networking/tls_sp_c32.o",
    "procps/top.o",
    "selinux/chcon.o",
    "selinux/setfiles.o",
    "util-linux/mkswap.o",
))


# Reviewable cross-ISA GNU fallback set from original BusyBox Kbuild
# and dual QEMU acceptance #39447330925. New MiniC successes remove entries;
# a previously accepted MiniC owner reverting to GNU is a hard regression.
CROSS_APPROVED_GCC_FALLBACKS = {
    "aarch64": frozenset((
        "coreutils/od.o",
        "editors/awk.o",
        "libbb/compare_string_array.o",
        "libbb/obscure.o",
        "libbb/strrstr.o",
        "libbb/vfork_daemon_rexec.o",
        "libbb/xconnect.o",
        "miscutils/devfsd.o",
        "miscutils/devmem.o",
        "miscutils/hdparm.o",
        "networking/ifconfig.o",
        "networking/ntpd.o",
        "procps/top.o",
    )),
    "loongarch64": frozenset((
        "archival/libarchive/decompress_bunzip2.o",
        "coreutils/od.o",
        "editors/awk.o",
        "libbb/compare_string_array.o",
        "libbb/obscure.o",
        "libbb/pw_encrypt.o",
        "libbb/strrstr.o",
        "libbb/vfork_daemon_rexec.o",
        "libbb/xconnect.o",
        "miscutils/devfsd.o",
        "miscutils/devmem.o",
        "miscutils/hdparm.o",
        "networking/ifconfig.o",
        "networking/ntpd.o",
        "procps/top.o",
    )),
}


def certified_owner_coverage(mini_owners, gcc_fallbacks):
    """Fail closed on a regressed owner or a different original Kbuild set.

    A newly supported former GCC fallback is allowed (coverage improves),
    but newly GCC-owned objects or missing/duplicated owners are not.
    """
    minic = list(mini_owners)
    gnu = list(gcc_fallbacks)
    names = minic + gnu
    return (len(names) == CERTIFIED_KBUILD_OWNERS and
            len(set(names)) == CERTIFIED_KBUILD_OWNERS and
            len(minic) >= CERTIFIED_MINIC_OWNERS and
            set(gnu).issubset(CERTIFIED_GCC_FALLBACKS))


def log(msg):
    print(msg, flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--proxy", required=True)
    parser.add_argument("--host-cc", default="cc")
    args = parser.parse_args()
    root = Path(args.source).resolve()
    report = Path(args.report).resolve()
    original = report / "bulk-gcc-reference" / "busybox"
    original.parent.mkdir(parents=True, exist_ok=True)
    executable = root / "busybox"
    if not executable.is_file():
        log("BUSYBOX_MIXED_BULK=BLOCKED stage=original-busybox-not-found")
        return 1
    shutil.copy2(executable, original)
    oracle = run_cases(original, report)
    expected_cases = 36 if os.environ.get("BUSYBOX_ALLYES_QEMU") else 11
    if len(oracle) != expected_cases or any(item[0] for item in oracle.values()):
        log("BUSYBOX_MIXED_BULK=BLOCKED stage=original-GNU-runtime-oracle")
        return 1
    manifest = {}
    skipped = []
    for result in sorted((report / "per-tu").glob("*/result.json")):
        row = json.loads(result.read_text(encoding="utf-8"))
        obj_name = row.get("object")
        if not obj_name:
            continue
        obj = (root / obj_name).resolve()
        mini = result.parent / "mini.o"
        if all(row.get(k) == 0 for k in ("gcc", "cpp", "minic", "assembler")) \
                and obj.is_file() and mini.is_file() and obj != mini.resolve():
            # A successful assembler is not sufficient evidence of a
            # linkable MiniC object. BusyBox df.c exposed unresolved private
            # inline-specialization symbols. Keep the real GNU owner for
            # these only, after auditing the actual target ELF with nm.
            if os.environ.get("BUSYBOX_ALLYES_MINIC_TARGET", "x86_64-linux") != "x86_64-linux":
                prefix = os.environ.get("BUSYBOX_ALLYES_CROSS_PREFIX", "")
                nm = subprocess.run([prefix + "nm", "-u", str(mini)],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    check=False)
                if nm.returncode != 0:
                    log("BUSYBOX_MIXED_BULK=BLOCKED stage=target-nm object=%s" % obj_name)
                    return 1
                if b"__minic_inline_spec_" in nm.stdout:
                    skipped.append((obj_name, "unresolved-inline", row.get("cpp")))
                    log("BUSYBOX_MIXED_BULK_INLINE_FALLBACK object=%s" % obj_name)
                    continue
            if str(obj) in manifest:
                log("BUSYBOX_MIXED_BULK=FAIL stage=duplicate-Kbuild-owner object=%s" % obj)
                return 1
            manifest[str(obj)] = str(mini)
        else:
            skipped.append((obj_name, row.get("minic"), row.get("cpp")))
    log("BUSYBOX_MIXED_BULK_SOURCE=DISCOVERY original_Kbuild_owners=%d "
        "eligible_MiniC=%d GCC_fallback=%d" %
        (len(manifest) + len(skipped), len(manifest), len(skipped)))
    for name, minic, cpp in skipped:
        log("BUSYBOX_MIXED_BULK_GCC_FALLBACK object=%s minic=%s cpp=%s" %
            (name, minic, cpp))
    mini_names = [Path(name).relative_to(root).as_posix() for name in manifest]
    fallback_names = [name for name, _minic, _cpp in skipped]
    cross_target = os.environ.get("BUSYBOX_ALLYES_MINIC_TARGET",
                                  "x86_64-linux") != "x86_64-linux"
    if cross_target:
        # First cross-ISA *experimental* all-owner real-Kbuild candidate.
        # Do not use x86's immutable 593/581 certification to invent a
        # cross count. Require broad genuine coverage and unique paths;
        # after GNU/QEMU success the exact cohort can be promoted.
        owner_count = len(mini_names) + len(fallback_names)
        target_name = os.environ["BUSYBOX_ALLYES_MINIC_TARGET"].split("-")[0]
        approved = CROSS_APPROVED_GCC_FALLBACKS.get(target_name)
        target_floor = {"aarch64": 568, "loongarch64": 566}.get(target_name)
        accepted = (owner_count == 581 and target_floor is not None
                    and len(mini_names) >= target_floor
                    and len(set(mini_names + fallback_names)) == owner_count
                    and approved is not None
                    and set(fallback_names).issubset(approved))
        if not accepted:
            log("BUSYBOX_MIXED_BULK=BLOCKED stage=cross-owner-inventory "
                "original_Kbuild_owners=%d MiniC=%d GNU_fallbacks=%d unapproved=%s" %
                (owner_count, len(mini_names), len(fallback_names),
                 sorted(set(fallback_names) - (approved or frozenset()))))
            return 1
        log("BUSYBOX_MIXED_BULK_CROSS_COHORT=DISCOVERY original_Kbuild_owners=%d"
            " MiniC_qualified=%d GNU_fallbacks=%d full_MiniC=NO" %
            (owner_count, len(mini_names), len(fallback_names)))
    elif not certified_owner_coverage(mini_names, fallback_names):
        log("BUSYBOX_MIXED_BULK=BLOCKED stage=certified-owner-coverage "
            "mini=%d gcc=%d expected_persistent=%d approved_fallbacks=%d" %
            (len(mini_names), len(fallback_names), CERTIFIED_KBUILD_OWNERS,
             len(CERTIFIED_GCC_FALLBACKS)))
        return 1

    manifest_file = report / "bulk-kbuild-owner-manifest.json"
    manifest_file.write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8")
    original_sha = sha256(original)
    for path in manifest:
        Path(path).unlink()
    # Kbuild can keep old intermediate archive owners. Invalidate all
    # containing directories and their ancestors, not just a single applet
    # directory, to force a *new* real link with every MiniC object.
    ancestors = set()
    for name in manifest:
        parent = Path(name).parent
        while parent != root and root in parent.parents:
            ancestors.add(parent)
            parent = parent.parent
    for parent in sorted(ancestors, key=lambda p: -len(p.parts)):
        for file in ("built-in.o", "lib.a"):
            aggregate = parent / file
            if aggregate.exists():
                aggregate.unlink()
    for file in ("busybox", "busybox_unstripped"):
        path = root / file
        if path.exists():
            path.unlink()

    env = os.environ.copy()
    for name in ("MAKEFLAGS", "MAKEOVERRIDES", "MFLAGS", "GNUMAKEFLAGS", "CFLAGS"):
        env.pop(name, None)
    env.update({"BUSYBOX_ALLYES_HOST_CC": args.host_cc,
                "BUSYBOX_ALLYES_REPORT_ROOT": str(report),
                "BUSYBOX_ALLYES_SKIP_SHADOW": "1",
                "BUSYBOX_ALLYES_REPLACE_MAP": str(manifest_file)})
    with (report / "bulk.make.out").open("wb") as stdout, \
         (report / "bulk.make.err").open("wb") as stderr:
        rc = subprocess.run(["make", "-j4", "CC=" + args.proxy, "busybox"],
                            cwd=root, env=env, stdout=stdout, stderr=stderr,
                            check=False).returncode
    if rc or not executable.is_file():
        logs = (report / "bulk.make.err").read_text(errors="replace")
        log("BUSYBOX_MIXED_BULK=BLOCKED stage=original-Kbuild-link exit=%d "
            "last_stderr=%r" % (rc, logs[-4000:]))
        return 1
    events = report / "combined-events"
    verified = 0
    for obj, mini in manifest.items():
        key = hashlib.sha256(obj.encode("utf-8")).hexdigest()[:20]
        event = events / (key + ".replace.txt")
        if not event.is_file() or not Path(obj).is_file() or sha256(obj) != sha256(mini):
            log("BUSYBOX_MIXED_BULK=BLOCKED stage=Kbuild-owner-not-MiniC "
                "verified=%d expected=%d object=%s event=%s" %
                (verified, len(manifest), obj, event.is_file()))
            return 1
        verified += 1
    log("BUSYBOX_MIXED_BULK_REAL_OWNERS=PASS total=%d SHA256_verified=%d "
        "original_Kbuild=1" % (len(manifest), verified))
    new_sha = sha256(executable)
    if new_sha == original_sha:
        log("BUSYBOX_MIXED_BULK=BLOCKED stage=GNU-binary-unchanged")
        return 1
    mixed = run_cases(executable, report)
    failures = [key for key, oracle_result in oracle.items() if mixed.get(key) != oracle_result]
    for key in failures[:8]:
        log("BUSYBOX_MIXED_BULK_RUNTIME_DIFF fixture=%s GCC=%r MiniC=%r" %
            (key, oracle[key], mixed.get(key)))
    log("BUSYBOX_MIXED_BULK_RUNTIME=DISCOVERY fixtures=%d matches=%d differences=%d" %
        (len(oracle), len(oracle) - len(failures), len(failures)))
    if failures:
        log("BUSYBOX_MIXED_BULK=BLOCKED stage=runtime-differential")
        return 1
    if cross_target:
        log("BUSYBOX_MIXED_BULK_CROSS_RUNTIME=PASS cases=36 GNU_QEMU_byte_exact=1")
    log("BUSYBOX_MIXED_BULK=PASS MiniC_owners=%d GCC_fallback=%d "
        "original_full_Kbuild=1 eleven_fixture_byte_exact=1 "
        "MiniC_and_GNU_binary_differ=1 full_all_MiniC=NO" %
        (verified, len(skipped)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
