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


def check_legacy_gate_removed() -> None:
    # The original legacy route hid shard failures behind a successful-needs
    # condition. Its immutable original GNU input is fully covered by the
    # canonical pinned gate, whose artifact identity and strict census are
    # checked below; do not resurrect two entry points for the same corpus.
    for legacy in ("minias-linux-generated-3x7-replay.yml",
                   "minias-linux-cached-kbuild-replay.yml"):
        status = subprocess.run(
            ["git", "ls-files", "--error-unmatch", f".github/workflows/{legacy}"],
            cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        if status.returncode == 0:
            raise AssertionError(f"redundant Linux 3x7 entry point restored: {legacy}")
        if status.returncode != 1:
            raise AssertionError("cannot inspect tracked CI files in this checkout")


def check_canonical_gate() -> None:
    source = (WORKFLOWS / "minias-linux-pinned-3x7-replay.yml").read_text()
    if "[minias-replay-3x7]" not in source:
        raise AssertionError("canonical exact-head trigger missing")
    if "run-id: '40658346252'" not in source:
        raise AssertionError("canonical gate no longer uses the pinned GNU producer")
    shard = job(source, "shard")
    aggregate = job(source, "aggregate")
    if "shard: [0, 1, 2, 3, 4, 5, 6]" not in shard:
        raise AssertionError("canonical Kbuild replay lost seven-way shard coverage")
    if "'[\"x86_64\",\"aarch64\",\"loongarch64\"]'" not in shard:
        raise AssertionError("canonical Kbuild replay lost the three-target matrix")
    if "assert digest == hashes[target]" not in shard:
        raise AssertionError("canonical source manifest SHA is no longer pinned")
    scoped_jobs = shard + aggregate
    for token in ("--strict", "authentic_incbin_frozen", "manifest",
                  "config_sha256", "minias-linux-kbuild-seven-v1.py aggregate"):
        if token not in scoped_jobs:
            raise AssertionError(f"canonical Kbuild gate missing: {token}")
    if not re.search(r"Preserve shard diagnostics[^\n]*\n\s+if: always\(\)\n",
                     shard):
        raise AssertionError("failed 3x7 shard may lose its results.json")
    if "needs.shard.result == 'success'" in aggregate:
        raise AssertionError("failed shard suppresses the entire error census")
    if "if: always() && needs.shard.result != 'skipped'" not in aggregate:
        raise AssertionError("canonical census must run after failed shards")
    for token in ("Preserve partial shard inventory", "MINIAS_SHARD_RESULT",
                  "collection-diagnostics.json", "missing_shards",
                  "if: always()"):
        if token not in aggregate:
            raise AssertionError(f"partial failed-shard diagnostics missing: {token}")
    if not re.search(r"Require all selected 7-way shards[^\n]*\n\s+if: always\(\)",
                     aggregate):
        raise AssertionError("failed artifact download can suppress strict final gate")


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
    check_legacy_gate_removed()
    check_canonical_gate()
    check_exact_head_four_target_gnu_gates()
    print("MINIAS_CI_ROUTING=PASS canonical_3x7=strict "
          "legacy_3x7=removed preserved_diagnostics=1 "
          "shared_elf=canonical_reader_objcopy_strip "
          "fast_pr=exact_head GNU_stress_targets=4")


if __name__ == "__main__":
    main()
