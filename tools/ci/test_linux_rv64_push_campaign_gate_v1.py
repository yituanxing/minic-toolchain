#!/usr/bin/env python3
"""Execute the actual RV64 campaign gate against tiny synthetic Git histories.

This never builds Linux, runs QEMU, or changes a real repository ref.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/linux-runtime-gcc-baseline.yml"
BRANCH = "refactor/minic-target-language-boundary"
RUNTIME = "tools/ci/linux-rv64-runtime-trigger.txt"
STRICT12 = "tools/ci/linux-rv64-p1-eight-trigger.txt"


def actual_gate() -> str:
    text = WORKFLOW.read_text()
    anchor = "        id: decide\n        shell: bash\n        run: |\n"
    assert text.count(anchor) == 1, "RV64 exact-head gate location changed"
    block = text.split(anchor, 1)[1].split("\n  # A cached original GNU", 1)[0]
    return textwrap.dedent(block).strip() + "\n"


def git(work: Path, *args: str) -> None:
    subprocess.run(["git", *args], cwd=work, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def verdict(event: str, ref: str, head_ref: str, changed: tuple[str, ...],
            *, title_full: bool = False, unrelated_last: bool = False) -> dict[str, str]:
    script = actual_gate()
    replacements = {
        "${{ github.event_name }}": event,
        "${{ github.ref }}": ref,
        "${{ github.head_ref }}": head_ref,
        "${{ contains(github.event.pull_request.title, '[minic-full]') }}":
            "true" if title_full else "false",
    }
    for key, value in replacements.items():
        assert key in script, f"workflow expression changed: {key}"
        script = script.replace(key, value)
    assert "${{" not in script, "an unbound GitHub expression remained"
    with tempfile.TemporaryDirectory(prefix="minic-rv64-push-gate-") as name:
        work = Path(name)
        for path in (RUNTIME, STRICT12):
            target = work / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("initial\n")
        git(work, "init", "-q")
        git(work, "config", "user.email", "codex@example.invalid")
        git(work, "config", "user.name", "Gate Test")
        git(work, "add", ".")
        git(work, "commit", "-qm", "seed")
        for path in changed:
            (work / path).write_text("exact-head consent\n")
        if not changed:
            (work / "unrelated").write_text("unrelated\n")
        git(work, "add", ".")
        git(work, "commit", "-qm", "new head")
        if unrelated_last:
            (work / "unrelated-next").write_text("unrelated\n")
            git(work, "add", ".")
            git(work, "commit", "-qm", "latest head without consent")
        receipt = work / "gate.out"
        env = dict(os.environ, GITHUB_OUTPUT=str(receipt))
        p = subprocess.run(["bash", "-c", script], cwd=work, env=env,
                           text=True, capture_output=True)
        if p.returncode != 0:
            raise AssertionError(f"real gate script failed: {p.stderr}")
        return dict(line.split("=", 1) for line in receipt.read_text().splitlines())


class RV64PushGate(unittest.TestCase):
    def test_push_two_markers_exact_branch_admitted(self):
        self.assertEqual(verdict("push", f"refs/heads/{BRANCH}", "",
                                 (RUNTIME, STRICT12)),
                         {"run": "true", "p1_gdb": "false", "p1_eight": "true"})

    def test_push_runtime_only_refused(self):
        self.assertEqual(verdict("push", f"refs/heads/{BRANCH}", "",
                                 (RUNTIME,))["run"], "false")

    def test_push_wrong_branch_refused(self):
        self.assertEqual(verdict("push", "refs/heads/main", "",
                                 (RUNTIME, STRICT12))["run"], "false")

    def test_stale_marker_one_commit_later_refused(self):
        self.assertEqual(verdict("push", f"refs/heads/{BRANCH}", "",
                                 (RUNTIME, STRICT12), unrelated_last=True)["run"],
                         "false")

    def test_pr_same_branch_cannot_duplicate_push_campaign(self):
        self.assertEqual(verdict("pull_request", "refs/pull/674/merge", BRANCH,
                                 (RUNTIME, STRICT12))["run"], "false")

    def test_legacy_pr_other_branch_kept_and_minic_full_excluded(self):
        other = "another-review-branch"
        self.assertEqual(verdict("pull_request", "refs/pull/999/merge", other,
                                 (RUNTIME,))["run"], "true")
        self.assertEqual(verdict("pull_request", "refs/pull/999/merge", other,
                                 (RUNTIME,), title_full=True)["run"], "false")


if __name__ == "__main__":
    unittest.main()
