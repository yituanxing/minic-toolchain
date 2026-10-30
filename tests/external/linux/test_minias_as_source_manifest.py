"""Fail-closed parser and exact comparison for GCC-produced .s manifests."""

import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

import minias_as_source_manifest as manifest


DIGEST_A = "a" * 64
DIGEST_B = "b" * 64


class SourceManifestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="minias-as-manifest-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write_tsv(self, name, rows, header=True):
        path = self.root / (name + ".tsv")
        contents = ["object\tassembly_sha256"] if header else []
        contents.extend(f"{owner}\t{digest}" for owner, digest in rows)
        path.write_text("\n".join(contents) + "\n", encoding="utf-8")
        return path

    def write_jsonl(self, name, rows):
        path = self.root / (name + ".jsonl")
        path.write_text("".join(json.dumps({"object": owner,
                                           "assembly_sha256": digest}) + "\n"
                                for owner, digest in rows), encoding="utf-8")
        return path

    def assert_rejected(self, path, phrase=None):
        with self.assertRaises(manifest.ManifestError) as ctx:
            manifest.read_manifest(path)
        if phrase:
            self.assertIn(phrase, str(ctx.exception))

    def test_identical_manifests_accept_jsonl_tsv_cross_format(self):
        rows = [("arch/arm64/kernel/entry.o", DIGEST_A),
                ("drivers/tty/serial/amba-pl011.o", DIGEST_B)]
        mini = manifest.read_manifest(self.write_jsonl("mini", rows))
        gnu = manifest.read_manifest(self.write_tsv("gnu", reversed(rows)))
        self.assertEqual(manifest.compare(mini, gnu, 2), 2)

    def test_one_production_object_missing_in_either_lane_fails(self):
        one = {"arch/arm64/kernel/entry.o": DIGEST_A}
        two = {**one, "mm/vmalloc.o": DIGEST_B}
        with self.assertRaisesRegex(manifest.ManifestError, "object set mismatch"):
            manifest.compare(one, two, 1)
        with self.assertRaisesRegex(manifest.ManifestError, "object set mismatch"):
            manifest.compare(two, one, 1)

    def test_digest_mismatch_fails(self):
        with self.assertRaisesRegex(manifest.ManifestError, "SHA mismatch"):
            manifest.compare({"kernel/sched/core.o": DIGEST_A},
                             {"kernel/sched/core.o": DIGEST_B}, 1)

    def test_duplicate_owner_fails_even_when_hash_equal(self):
        path = self.write_tsv("duplicate", [("kernel/sched/core.o", DIGEST_A)] * 2)
        self.assert_rejected(path, "duplicate production owner")

    def test_json_duplicate_fields_fail(self):
        path = self.root / "duplicate.jsonl"
        path.write_text('{"object":"a.o","object":"b.o",'
                        f'"assembly_sha256":"{DIGEST_A}"}}\n')
        self.assert_rejected(path, "duplicate JSON field")

    def test_absolute_parent_dot_and_unormalized_owners_fail(self):
        invalid = ["/tmp/obj.o", "../obj.o", "a/../obj.o", "./obj.o",
                   "a//obj.o", "a/./obj.o", "a\\obj.o", "a/obj.s",
                   "-bad.o", "x.o ", "x.o\r", "x.o\tother"]
        for owner in invalid:
            with self.subTest(owner=owner):
                with self.assertRaises(manifest.ManifestError):
                    manifest.canonical_owner(owner, "test")

    def test_missing_uppercase_and_malformed_sha_fail(self):
        for value in ("", "f" * 63, "G" * 64, "A" * 64, None, 17):
            with self.subTest(value=value):
                with self.assertRaises(manifest.ManifestError):
                    manifest.require_digest(value, "test")

    def test_empty_tsv_and_jsonl_fail(self):
        self.assert_rejected(self.write_tsv("empty", []), "no objects")
        self.assert_rejected(self.write_jsonl("empty", []), "empty manifest")

    def test_malformed_tsv_row_or_header_fails(self):
        self.assert_rejected(self.write_tsv("bad-header", [("a.o", DIGEST_A)], False),
                             "schema header")
        path = self.root / "bad-row.tsv"
        path.write_text("object\tassembly_sha256\na.o\n")
        self.assert_rejected(path, "exactly two TSV fields")

    def test_schema_extra_fields_fail(self):
        path = self.root / "extra.jsonl"
        path.write_text(json.dumps({"object": "a.o", "assembly_sha256": DIGEST_A,
                                    "fake_pass": True}) + "\n")
        self.assert_rejected(path, "schema")

    def test_minimum_object_count_cannot_be_skipped(self):
        one = {"a.o": DIGEST_A}
        with self.assertRaisesRegex(manifest.ManifestError, "too few objects"):
            manifest.compare(one, one, 2)
        with self.assertRaisesRegex(manifest.ManifestError, "at least 1"):
            manifest.compare(one, one, 0)

    def test_cli_success_marker_and_failure_status(self):
        rows = [("kernel/sched/core.o", DIGEST_A)]
        mini = self.write_tsv("mini", rows)
        gnu = self.write_tsv("gnu", rows)
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            self.assertEqual(manifest.main(["--mini-manifest", str(mini),
                                            "--gnu-manifest", str(gnu),
                                            "--arch", "x86_64",
                                            "--min-objects", "1"]), 0)
        self.assertIn("MINIAS_LINUX_ALL_ASM_SOURCES_PASS", buffer.getvalue())
        gnu = self.write_tsv("gnu", [("kernel/sched/core.o", DIGEST_B)])
        with contextlib.redirect_stderr(buffer):
            self.assertEqual(manifest.main(["--mini-manifest", str(mini),
                                            "--gnu-manifest", str(gnu),
                                            "--arch", "x86_64"]), 1)
        self.assertIn("MINIAS_LINUX_ALL_ASM_SOURCES_FAIL", buffer.getvalue())


if __name__ == "__main__":
    unittest.main()
