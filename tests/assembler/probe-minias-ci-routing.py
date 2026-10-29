#!/usr/bin/env python3
"""Static CI boundary checks for MiniAS and shared ELF consumers.

This check does not dispatch GitHub workflows or depend on a Linux corpus.
"""

from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"


def job(source: str, name: str) -> str:
    tail = source.split("\njobs:\n", 1)[1]
    prefix = f"  {name}:\n"
    if tail.count(prefix) != 1:
        raise AssertionError(f"missing/duplicate workflow job: {name}")
    rest = tail.split(prefix, 1)[1]
    next_job = re.search(r"(?m)^  [a-zA-Z][\w-]*:\s*$", rest)
    return rest[:next_job.start()] if next_job else rest


def check_generated_gate() -> None:
    source = (WORKFLOWS / "minias-linux-generated-3x7-replay.yml").read_text()
    if "Candidate Gate (Legacy)" not in source.splitlines()[0]:
        raise AssertionError("legacy scan claims an ambiguous acceptance level")
    shard = job(source, "shard")
    for dimension in ("target: [x86_64, aarch64, loongarch64]",
                      "shard: [0, 1, 2, 3, 4, 5, 6]"):
        if dimension not in shard:
            raise AssertionError(f"original Linux matrix incomplete: {dimension}")
    aggregate = job(source, "aggregate")
    if "needs.shard.result == 'success'" not in aggregate:
        raise AssertionError("legacy aggregate does not depend on all shards")
    if "minias-linux-kbuild-seven-v1.py aggregate" not in aggregate:
        raise AssertionError("legacy result aggregator missing")
    if not re.search(r"(?m)^\s+--strict\s*\\$", aggregate):
        raise AssertionError("legacy aggregate permits false-green candidates")
    if "if: always()" not in aggregate:
        raise AssertionError("strict failure can suppress diagnostics artifact")


def check_canonical_gate() -> None:
    source = (WORKFLOWS / "minias-linux-pinned-3x7-replay.yml").read_text()
    if "[minias-replay-3x7]" not in source:
        raise AssertionError("canonical exact-head trigger missing")
    scoped_jobs = job(source, "aggregate") + job(source, "shard")
    for token in ("--strict", "needs.shard.result == 'success'",
                  "authentic_incbin_frozen", "manifest", "config_sha256",
                  "minias-linux-kbuild-seven-v1.py aggregate"):
        if token not in scoped_jobs:
            raise AssertionError(f"canonical Kbuild gate missing: {token}")


def main() -> None:
    subprocess.run([sys.executable,
                    str(ROOT / "tools/ci/check_ci_miniobjcopy_elf_route_v1.py")],
                   check=True)
    subprocess.run([sys.executable,
                    str(ROOT / "tools/ci/select_miniobjcopy_elf_route_v1.py"),
                    "--self-test"], check=True)
    check_generated_gate()
    check_canonical_gate()
    print("MINIAS_CI_ROUTING=PASS canonical_3x7=strict "
          "legacy_3x7=strict_aggregate preserved_diagnostics=1 "
          "shared_elf=canonical_reader_objcopy_strip")


if __name__ == "__main__":
    main()
