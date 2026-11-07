#!/usr/bin/env python3
"""Static and fail-closed tests for the branch-scoped RV32 Linux source seed."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest

import yaml


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/linux-runtime-gcc-baseline.yml"
SOURCE_KEY = "linux-source-6.6.143-dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932-v1"


class RV32CurrentSourceFallbackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.jobs = yaml.safe_load(WORKFLOW.read_text())["jobs"]
        cls.producer = cls.jobs["rv32i-kbuild-entry"]["steps"]

    def get_step(self, name: str) -> dict:
        return next(step for step in self.producer if step.get("name") == name)

    def test_source_seed_and_shared_cache_identity(self) -> None:
        producer = self.producer
        old = next(step for step in producer if step.get("uses") == "./.github/actions/linux-runtime-restore-fixture")
        self.assertIn("!= 'true'", old["if"])
        restore = self.get_step("Restore pinned RV32 source when already available on this branch")
        seed = self.get_step("Reconstitute SHA-pinned original RV32 source after branch cache miss")
        verify = self.get_step("Verify source identity before any RV32 GNU configuration")
        save = self.get_step("Save verified RV32 original source for seven dependent workers")
        config = self.get_step("Derive original RV32 Kconfig without compiling any kernel objects")
        self.assertEqual(restore["with"]["key"], SOURCE_KEY)
        self.assertEqual(save["with"]["key"], SOURCE_KEY)
        self.assertNotIn("fail-on-cache-miss", restore["with"])
        self.assertIn("cache-hit != 'true'", seed["if"])
        self.assertIn("cache-hit != 'true'", save["if"])
        self.assertLess(producer.index(restore), producer.index(seed))
        self.assertLess(producer.index(seed), producer.index(verify))
        self.assertLess(producer.index(save), producer.index(config))
        self.assertIn("sha256sum -c -", seed["run"])
        self.assertIn("--proto '=https'", seed["run"])
        self.assertIn("https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.143.tar.xz", seed["run"])
        for consumer in ("gcc-shards", "gcc-shard-verify"):
            steps = self.jobs[consumer]["steps"]
            self.assertTrue(any(step.get("uses") == "./.github/actions/linux-runtime-restore-fixture" and
                                step.get("with", {}).get("fixture") == "source" for step in steps))
            self.assertFalse(any(step.get("with", {}).get("fixture") == "frozen" for step in steps))
        # RV64's independently gated producer already seeds this identical
        # SHA-bound key on its own current-source lane before its seven shards.
        rv64 = self.jobs["rv64-current-gnu-golden"]["steps"]
        self.assertTrue(any(step.get("with", {}).get("key") == SOURCE_KEY and
                            step.get("uses") == "actions/cache/save@v4" for step in rv64))

    def test_modified_download_rejected_before_extract(self) -> None:
        with tempfile.TemporaryDirectory(prefix="rv32-source-negative-") as tmp:
            workspace = Path(tmp)
            fake_bin = workspace / "bin"
            fake_bin.mkdir()
            fake_curl = fake_bin / "curl"
            fake_curl.write_text("#!/bin/bash\nfor ((i=1;i<=$#;i++)); do if [[ ${!i} == -o ]]; then j=$((i+1)); printf 'corrupt' > \"${!j}\"; exit 0; fi; done\nexit 99\n")
            fake_curl.chmod(0o755)
            env = dict(os.environ, PATH=f"{fake_bin}:{os.environ['PATH']}",
                       GITHUB_WORKSPACE=str(workspace), RUNNER_TEMP=str(workspace))
            script = self.get_step("Reconstitute SHA-pinned original RV32 source after branch cache miss")["run"]
            p = subprocess.run(["bash", "-c", script], env=env, text=True,
                               capture_output=True, timeout=10)
            self.assertNotEqual(p.returncode, 0)
            self.assertIn("FAILED", p.stdout + p.stderr)
            self.assertFalse((workspace / "build/linux-expanded-kbuild-v0/linux-6.6.143").exists())

    def test_cached_wrong_source_release_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="rv32-source-release-") as tmp:
            workspace = Path(tmp)
            source = workspace / "build/linux-expanded-kbuild-v0/linux-6.6.143"
            source.mkdir(parents=True)
            source.joinpath("Makefile").write_text("VERSION = 6\nPATCHLEVEL = 6\nSUBLEVEL = 142\n")
            script = self.get_step("Verify source identity before any RV32 GNU configuration")["run"]
            script = script.replace("${{ steps.rv32_current_source_cache.outputs.cache-hit }}", "true")
            p = subprocess.run(["bash", "-c", script], cwd=workspace, text=True,
                               capture_output=True, timeout=10)
            self.assertNotEqual(p.returncode, 0)
            source.joinpath("Makefile").write_text("VERSION = 6\nPATCHLEVEL = 6\nSUBLEVEL = 143\n")
            p = subprocess.run(["bash", "-c", script], cwd=workspace, text=True,
                               capture_output=True, timeout=10)
            self.assertEqual(p.returncode, 0, p.stderr)
            self.assertIn("RV32I_ORIGINAL_SOURCE=VERIFIED", p.stdout)


if __name__ == "__main__":
    unittest.main()
