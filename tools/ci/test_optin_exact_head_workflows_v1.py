#!/usr/bin/env python3
"""Exercise the exact-HEAD opt-in gates for costly optional CI diagnostics."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

import yaml


ROOT = Path(__file__).resolve().parents[2]
CASES = (
    (".github/workflows/minic-rv32-gpu-single-object-diff.yml",
     "frozen-gpu-eligibility", "single-object", "run_diagnostic",
     "[rv32-gpu-frozen-diff]"),
    (".github/workflows/minic-linux-x86-kvm-preflight.yml",
     "nested-vmx-eligibility", "x86-kvm-real-guest-preflight",
     "run_preflight", "[x86-kvm-nested-preflight]"),
)


def git(directory: Path, *args: str) -> None:
    subprocess.run(["git", *args], cwd=directory, check=True,
                   capture_output=True, text=True)


class ExactHeadGateTests(unittest.TestCase):
    def test_both_opt_in_workflows(self) -> None:
        for filename, gate_name, consumer_name, output, marker in CASES:
            with self.subTest(workflow=filename):
                workflow = yaml.safe_load((ROOT / filename).read_text())
                triggers = workflow.get("on", workflow.get(True))
                self.assertIn("workflow_dispatch", triggers)
                self.assertIn("pull_request", triggers)
                self.assertFalse(triggers["pull_request"] or {})
                gate = workflow["jobs"][gate_name]
                consumer = workflow["jobs"][consumer_name]
                self.assertEqual(consumer["needs"], gate_name)
                self.assertEqual(consumer["if"],
                                 f"needs.{gate_name}.outputs.{output} == 'true'")
                scope = next(s for s in gate["steps"] if s.get("id") == "scope")
                self.assertEqual(gate["outputs"][output],
                                 f"${{{{ steps.scope.outputs.{output} }}}}")
                self.assertEqual(scope["env"]["EVENT_NAME"],
                                 "${{ github.event_name }}")
                self.assertEqual(gate["steps"][0]["with"]["ref"],
                                 "${{ github.event.pull_request.head.sha || github.sha }}")
                with tempfile.TemporaryDirectory() as dirname:
                    work = Path(dirname)
                    git(work, "init", "-q")
                    git(work, "config", "user.name", "CI Gate Test")
                    git(work, "config", "user.email", "ci-test@example.invalid")

                    def commit(message: str) -> None:
                        git(work, "commit", "-q", "--allow-empty", "-m", message)

                    def run(event: str, allowed: bool) -> None:
                        result_file = work / "output.txt"
                        result_file.write_text("")
                        environment = dict(os.environ, EVENT_NAME=event,
                                           GITHUB_OUTPUT=str(result_file))
                        proc = subprocess.run(["bash", "-c", scope["run"]],
                                              cwd=work, env=environment,
                                              capture_output=True, text=True)
                        self.assertEqual(proc.returncode, 0, proc.stderr)
                        expected = f"{output}={'true' if allowed else 'false'}\n"
                        self.assertEqual(result_file.read_text(), expected)
                        if not allowed:
                            self.assertIn("SKIP reason=exact_head_unmarked", proc.stdout)

                    commit("unmarked base")
                    run("pull_request", False)
                    commit(f"opt in {marker}")
                    run("pull_request", True)
                    commit("unmarked newer head")
                    run("pull_request", False)
                    run("workflow_dispatch", True)


if __name__ == "__main__":
    unittest.main()
