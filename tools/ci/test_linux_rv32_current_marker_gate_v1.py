#!/usr/bin/env python3
"""Fail-closed one-shot RV32 marker and existing seven-shard routing tests."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
GATE = ROOT / "tools/ci/linux-rv32-current-marker-gate-v1.sh"
WORKFLOW = ROOT / ".github/workflows/linux-runtime-gcc-baseline.yml"
CFG = "4c6c19f79741a8a31ec52ad57b1a6bc56b81dd6996cbf5e1bdc63f587d85244c"
BRANCH = "refs/heads/refactor/minic-target-language-boundary"


def git(repo: Path, *args: str) -> str:
    p = subprocess.run(["git", *args], cwd=repo, check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return p.stdout.strip()


def marker(nonce: str = "20261107-1", config: str = CFG) -> str:
    return ("target=rv32i\nmode=original-1741-current-head\n"
            f"gnu_config_sha256={config}\nnonce={nonce}\n")


class CurrentRV32MarkerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="rv32-current-one-shot-")
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name)
        self.path = self.repo / "tools/ci/linux-rv32-current-runtime-trigger.txt"
        self.path.parent.mkdir(parents=True)
        git(self.repo, "init", "-q")
        git(self.repo, "config", "user.name", "Gate Test")
        git(self.repo, "config", "user.email", "gate@example.invalid")
        self.path.write_text(marker("20261107-1"))
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-q", "-m", "baseline")
        self.path.write_text(marker("20261107-2"))
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-q", "-m", "[linux-rv32i-kbuild] one shot")

    def run_gate(self, *, event: str = "push", branch: str = BRANCH,
                 sha: str | None = None) -> tuple[int, str, str]:
        output = self.repo / "github-output.txt"
        output.write_text("")
        env = dict(os.environ, GITHUB_EVENT_NAME=event, GITHUB_REF=branch,
                   GITHUB_SHA=sha or git(self.repo, "rev-parse", "HEAD"),
                   GITHUB_OUTPUT=str(output))
        p = subprocess.run(["bash", str(GATE)], cwd=self.repo, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           text=True)
        return p.returncode, p.stdout + p.stderr, output.read_text()

    def test_exact_head_push_accepts(self) -> None:
        code, log, out = self.run_gate()
        self.assertEqual(code, 0, log)
        self.assertIn("RV32I_CURRENT_EXACT_HEAD_GATE=true", log)
        self.assertEqual(out, "run=true\n")

    def test_pull_request_and_noncanonical_branch_rejected(self) -> None:
        for event, branch in (("pull_request", BRANCH),
                              ("push", "refs/heads/main"),
                              ("push", "refs/heads/agent/rv32")):
            with self.subTest(event=event, branch=branch):
                code, _, out = self.run_gate(event=event, branch=branch)
                self.assertEqual(code, 0)
                self.assertEqual(out, "run=false\n")

    def test_stale_marker_cannot_restart_on_next_source_commit(self) -> None:
        (self.repo / "source.c").write_text("int unchanged(void) {return 0;}\n")
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-q", "-m", "[linux-rv32i-kbuild] source edit")
        code, log, out = self.run_gate()
        self.assertNotEqual(code, 0)
        self.assertIn("tagged request missing valid current-HEAD marker", log)
        self.assertEqual(out, "")

    def test_missing_legacy_route_tag_rejected(self) -> None:
        self.path.write_text(marker("20261107-3"))
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-q", "-m", "ordinary feature push")
        self.assertEqual(self.run_gate()[2], "run=false\n")

    def test_noncanonical_config_rejected(self) -> None:
        self.path.write_text(marker("20261107-3", "0" * 64))
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-q", "-m", "[linux-rv32i-kbuild] tamper config")
        self.assertNotEqual(self.run_gate()[0], 0)

    def test_simultaneous_rv64_campaign_refused(self) -> None:
        self.path.write_text(marker("20261107-3"))
        (self.repo / "tools/ci/linux-rv64-runtime-trigger.txt").write_text("changed\n")
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-q", "-m", "[linux-rv32i-kbuild] both")
        self.assertNotEqual(self.run_gate()[0], 0)

    def test_sha_mismatch_fails_closed(self) -> None:
        code, log, _ = self.run_gate(sha="0" * 40)
        self.assertNotEqual(code, 0)
        self.assertIn("unexpected SHA mismatch", log)

    def test_existing_workflow_routes_entire_pipeline(self) -> None:
        import yaml

        d = yaml.safe_load(WORKFLOW.read_text())
        triggers = d.get("on", d.get(True))
        self.assertIn("refactor/minic-target-language-boundary",
                      triggers["push"]["branches"])
        self.assertIn("tools/ci/**", triggers["push"]["paths"])
        jobs = d["jobs"]
        self.assertIn("rv32-current-campaign-gate", jobs)
        self.assertEqual(jobs["rv32i-kbuild-entry"]["needs"],
                         "rv32-current-campaign-gate")
        for consumer in ("gcc-shards", "gcc-shard-verify"):
            self.assertIn("rv32-current-campaign-gate", jobs[consumer]["needs"])
        shards = jobs["gcc-shards"]
        self.assertIn("needs.rv32i-kbuild-entry.result == 'success'", shards["if"])
        self.assertIn("[linux-rv32i-kbuild]", shards["if"])
        shard_steps = "\n".join(str(step) for step in shards["steps"])
        self.assertIn("-head-$MINIC_EXPECTED_HEAD_SHA", shard_steps)
        self.assertIn("RV32I_LINUX_MODE=shard", shard_steps)
        self.assertIn("matrix.shard", shard_steps)
        self.assertIn("needs.rv32-current-campaign-gate.outputs.run", shard_steps)
        self.assertIn("RV32I_GNU_GOLDEN_CONFIG_SHA256", str(jobs["rv32i-kbuild-entry"]))
        self.assertIn("-eq 1741", str(jobs["rv32i-kbuild-entry"]))
        verify_steps = jobs["gcc-shard-verify"]["steps"]
        text = "\n".join(str(step) for step in verify_steps)
        for token in ("RV32I_LINUX_MODE=union", "RV32I_QEMU_P1=1",
                      "seq 1 12", "RV32I_CURRENT_HEAD_P1=PASS boots=12/12"):
            self.assertIn(token, text)


if __name__ == "__main__":
    unittest.main()
