#!/usr/bin/env python3
"""Test the expensive Linux seven-runner gate without GitHub credentials."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


WORKFLOW = Path(__file__).resolve().parents[2] / ".github/workflows/minic-linux-three-isa-seven-runtime-v1.yml"


def gate_script() -> str:
    lines = WORKFLOW.read_text().splitlines()
    gate_step = lines.index("      - name: Authenticate explicit marker push or manual dispatch")
    run_line = lines.index("        run: |", gate_step)
    body = []
    for line in lines[run_line + 1:]:
        if line and not line.startswith("          "):
            break
        body.append(line[10:] if line else "")
    return "\n".join(body) + "\n"


GIT_ORACLE = '''
git() {
  case "$1" in
    rev-parse)
      if [[ "$2" == --verify ]]; then return 0; fi
      echo 1558d0943
      ;;
    diff)
      if [[ "$*" == *"seven-source-refresh-trigger.txt"* ]]; then
        [[ "${MOCK_SOURCE_CHANGED:-false}" == true ]] && return 1 || return 0
      fi
      if [[ "$*" == *"seven-candidate-trigger.txt"* ]]; then
        [[ "${MOCK_CANDIDATE_CHANGED:-false}" == true ]] && return 1 || return 0
      fi
      return 0
      ;;
    log) printf '%s\\n' "${MOCK_HEAD_SUBJECT:-ordinary compiler fix}" ;;
    *) return 1 ;;
  esac
}
'''


class CandidateMarkerGateTest(unittest.TestCase):
    def run_gate(self, *, event="push", candidate_changed=True, source_changed=False,
                 candidate="target=aarch64\ninput_run=42366441631\nnonce=20261107-2\nmode=candidate-only\n",
                 head_subject="ordinary compiler fix") -> tuple[int, dict[str, str]]:
        script = GIT_ORACLE + gate_script().replace("${{ github.event_name }}", event)
        with tempfile.TemporaryDirectory(prefix="minic-candidate-trigger-") as tmp:
            work = Path(tmp)
            marker_dir = work / "tools/ci"
            marker_dir.mkdir(parents=True)
            (marker_dir / "linux-three-isa-seven-candidate-trigger.txt").write_text(candidate)
            (marker_dir / "linux-three-isa-seven-source-refresh-trigger.txt").write_text(
                "sources=all\nmode=input-only\nnonce=20261107-1\n")
            env = os.environ.copy()
            env.update(GITHUB_OUTPUT=str(work / "output"), GITHUB_STEP_SUMMARY=str(work / "summary"),
                       MINIC_LINUX_PINNED_INPUT_RUN="41181106144",
                       MOCK_CANDIDATE_CHANGED=str(candidate_changed).lower(),
                       MOCK_SOURCE_CHANGED=str(source_changed).lower(),
                       MOCK_HEAD_SUBJECT=head_subject)
            completed = subprocess.run(["bash", "-c", script], cwd=work, env=env,
                                       text=True, capture_output=True, timeout=10)
            data: dict[str, str] = {}
            if (work / "output").is_file():
                data = dict(line.split("=", 1) for line in (work / "output").read_text().splitlines())
            return completed.returncode, data

    def test_exact_push_targets_one_architecture(self) -> None:
        for target in ("x86_64", "aarch64", "loongarch64"):
            with self.subTest(target=target):
                code, data = self.run_gate(candidate=(
                    f"target={target}\ninput_run=42366441631\nnonce=20261107-2\nmode=candidate-only\n"))
                self.assertEqual(code, 0)
                self.assertEqual(data["run"], "true")
                self.assertEqual(data["source_only"], "false")
                self.assertEqual(data["regenerate"], "false")
                self.assertEqual(data["reuse_objects"], "false")
                self.assertEqual(data["producer_targets"], f'["{target}"]')
                self.assertEqual(data["receiver_targets"], f'["{target}"]')
                self.assertEqual(data["input_run"], "42366441631")

    def test_pull_request_uses_same_exact_head(self) -> None:
        code, data = self.run_gate(event="pull_request")
        self.assertEqual(code, 0)
        self.assertEqual(data["producer_targets"], '["aarch64"]')

    def test_unchanged_marker_cannot_run_expensive_candidates(self) -> None:
        code, data = self.run_gate(candidate_changed=False)
        self.assertEqual(code, 0)
        self.assertEqual(data["run"], "false")

    def test_existing_source_only_marker_still_skips_candidates(self) -> None:
        code, data = self.run_gate(candidate_changed=False, source_changed=True)
        self.assertEqual(code, 0)
        self.assertEqual(data["run"], "true")
        self.assertEqual(data["source_only"], "true")
        self.assertEqual(data["regenerate"], "true")
        self.assertEqual(data["producer_targets"], '["x86_64","aarch64","loongarch64"]')

    def test_invalid_or_missing_source_provenance_fails_closed(self) -> None:
        invalid = (
            "target=all\ninput_run=42366441631\nnonce=20261107-2\nmode=candidate-only\n",
            "target=aarch64\ninput_run=0\nnonce=20261107-2\nmode=candidate-only\n",
            "target=aarch64\ninput_run=bad\nnonce=20261107-2\nmode=candidate-only\n",
            "target=aarch64\ninput_run=42366441631\nnonce=bad\nmode=candidate-only\n",
            "target=aarch64\ninput_run=42366441631\nnonce=20261107-2\nmode=input-only\n",
            "target=aarch64\ninput_run=42366441631\nnonce=20261107-2\n",
        )
        for candidate in invalid:
            with self.subTest(candidate=candidate):
                self.assertNotEqual(self.run_gate(candidate=candidate)[0], 0)

    def test_no_overlap_with_source_refresh_or_reused_objects(self) -> None:
        self.assertNotEqual(self.run_gate(source_changed=True)[0], 0)
        self.assertNotEqual(self.run_gate(head_subject="[minic-reuse-objects]")[0], 0)
        self.assertNotEqual(self.run_gate(head_subject="[minic-regen-gnu]")[0], 0)


if __name__ == "__main__":
    unittest.main()
