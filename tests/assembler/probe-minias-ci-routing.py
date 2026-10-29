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


def check_exact_head_four_target_gnu_gates() -> None:
    """Fast PR checks must compile PR head, not GitHub's synthetic merge ref."""
    expected_ref = (
        "ref: ${{ github.event_name == 'pull_request' && "
        "github.event.pull_request.head.sha || github.sha }}"
    )
    expectations = {
        "minias-target-contract.yml": {
            "native-a0-rv32i": ("riscv",),
            "aarch64-vertical": ("aarch64",),
        },
        "minias-new-targets-oracle.yml": {
            "integer-gnu-differential": ("x86_64", "loongarch64"),
        },
    }
    for workflow, jobs in expectations.items():
        source = (WORKFLOWS / workflow).read_text()
        if "pull_request:" not in source:
            raise AssertionError(f"exact-head GNU oracle no longer runs for PRs: {workflow}")
        if "probe-typed-relax-four-target-stress.sh" not in source:
            raise AssertionError(f"new stress changes do not trigger: {workflow}")
        for name, targets in jobs.items():
            body = job(source, name)
            if body.count(expected_ref) != 1:
                raise AssertionError(f"{workflow}/{name} could use a synthetic merge HEAD")
            if "git rev-parse HEAD" not in body or "MINIAS_EXACT_HEAD=PASS" not in body:
                raise AssertionError(f"{workflow}/{name} lacks checkout SHA assertion")
            if "CFLAGS=-Werror" not in body:
                raise AssertionError(f"{workflow}/{name} lacks a strict exact-head build")
            for target in targets:
                invocation = (
                    f"ARCH={target} \\\n"
                    "            bash tests/assembler/probe-typed-relax-four-target-stress.sh"
                )
                if invocation not in body:
                    raise AssertionError(
                        f"{workflow}/{name} missing GNU typed relax stress for {target}"
                    )
            if name == "integer-gnu-differential":
                if "probe-x86-typed-branch-growth.sh" not in body:
                    raise AssertionError("GNU x86 branch growth oracle no longer runs")
                if "'tests/assembler/probe-x86-typed-branch-growth.sh'" not in source:
                    raise AssertionError("x86 branch-growth oracle changes do not trigger CI")
                metadata_regression = "probe-elf-semantic-reloc-metadata-order.py"
                if metadata_regression not in body:
                    raise AssertionError("strict GNU ELF relocation/metadata mutation oracle missing")
                if f"'tests/assembler/{metadata_regression}'" not in source:
                    raise AssertionError("ELF semantic oracle changes do not trigger CI")
            if name == "native-a0-rv32i":
                if "probe-mc-operand-lifetime.sh" not in body:
                    raise AssertionError("Common MCOperand lifecycle probe no longer runs")
                if "probe-mc-operand-failure-unwind.sh" not in body:
                    raise AssertionError("four-target malformed operand unwind no longer runs")
                if "probe-rv-typed-li-materialization.sh" not in body:
                    raise AssertionError("RV32/RV64 typed LI semantic oracle no longer runs")
                if "command -v riscv64-linux-gnu-as" not in body:
                    raise AssertionError("RV GNU semantic oracle could silently skip")


def main() -> None:
    subprocess.run([sys.executable,
                    str(ROOT / "tools/ci/check_ci_miniobjcopy_elf_route_v1.py")],
                   check=True)
    subprocess.run([sys.executable,
                    str(ROOT / "tools/ci/select_miniobjcopy_elf_route_v1.py"),
                    "--self-test"], check=True)
    check_generated_gate()
    check_canonical_gate()
    check_exact_head_four_target_gnu_gates()
    print("MINIAS_CI_ROUTING=PASS canonical_3x7=strict "
          "legacy_3x7=strict_aggregate preserved_diagnostics=1 "
          "shared_elf=canonical_reader_objcopy_strip "
          "fast_pr=exact_head GNU_stress_targets=4")


if __name__ == "__main__":
    main()
