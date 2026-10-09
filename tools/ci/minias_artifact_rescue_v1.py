#!/usr/bin/env python3
"""Rescue/verify historical MiniAS Actions artifacts with independently pinned hashes.

No GitHub API calls on --self-test or --verify-dir. --download is an explicit,
potentially >1 GiB operation. It DOES NOT publish to GitHub Releases.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener
import zipfile

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "tools/ci/minias-historical-artifacts-v1.json"
EXPECTED_NAMES = {
    *(f"frozen-linux-corpus-{s}" for s in
      ("first500", "new500", "next500", "next500b",
       "next500c", "next500d", "final352")),
    "minias-linux-sidecars-v1",
    "minias-ground-truth-c149-corpus",
}
EXPECTED_RUNS = {
    "frozen-linux-corpus-": 33186855250,
    "minias-linux-sidecars-v1": 33237304382,
    "minias-ground-truth-c149-corpus": 33248985705,
}

def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()

def sha256_stream(stream):
    digest = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
    return digest.hexdigest()

def check_catalog(data):
    assert data["schema_version"] == 1
    assert data["repository"] == "yituanxing/minic-toolchain"
    items = data["artifacts"]
    assert set(items) == EXPECTED_NAMES, "unexpected/missing historical artifact"
    assert len({item["id"] for item in items.values()}) == 9, "duplicate artifact IDs"
    for name, item in items.items():
        prefix = next((p for p in EXPECTED_RUNS if name.startswith(p)), None)
        assert prefix is not None and item["run_id"] == EXPECTED_RUNS[prefix]
        assert isinstance(item["id"], int) and item["id"] > 0
        assert isinstance(item["size_in_bytes"], int) and item["size_in_bytes"] > 0
        for digest in (item["zip_sha256"], *item["files"].values()):
            assert re.fullmatch(r"[a-f0-9]{64}", digest), (name, digest)
        assert item["expires_at"].startswith("2026-11-2")
    assert sum(bool(item["files"]) for item in items.values()) == 8
    return items

def verify_zip(archive, name, record):
    actual = sha256_file(archive)
    assert actual == record["zip_sha256"], (
        f"{name} outer ZIP SHA256 mismatch: {actual}"
    )
    with zipfile.ZipFile(archive) as zf:
        names = zf.namelist()
        assert len(names) == len(set(names)), f"{name} duplicated ZIP paths"
        for inner, expected in record["files"].items():
            assert inner in names, f"{name} missing {inner}"
            with zf.open(inner) as stream:
                assert sha256_stream(stream) == expected, (
                    f"{name} inner SHA256 mismatch: {inner}"
                )
    return actual

class NoCredentialForwardRedirect(HTTPRedirectHandler):
    """Never forward API Authorization headers to artifact-storage domains."""
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        dest = urlsplit(newurl)
        assert dest.scheme == "https", "refuse non-HTTPS redirect"
        # Actions artifacts are served from GitHub's content/blob storage.
        host = dest.hostname or ""
        assert (host == "api.github.com"
                or host.endswith(".githubusercontent.com")
                or host.endswith(".blob.core.windows.net")), (
                    f"unrecognized artifact redirect: {host}"
                )
        next_request = super().redirect_request(req, fp, code, msg, headers, newurl)
        if host != urlsplit(req.full_url).hostname:
            next_request.remove_header("Authorization")
        return next_request

def download_artifact(repo, name, record, dest, token):
    assert token, "set GH_TOKEN or GITHUB_TOKEN for authenticated artifact download"
    url = f"https://api.github.com/repos/{repo}/actions/artifacts/{record['id']}/zip"
    request = Request(url, headers={
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "minias-historical-artifact-rescue-v1",
    })
    opener = build_opener(NoCredentialForwardRedirect())
    partial = dest.with_name(dest.name + ".partial")
    try:
        with opener.open(request, timeout=120) as response, partial.open("wb") as sink:
            while block := response.read(1024 * 1024):
                sink.write(block)
        verify_zip(partial, name, record)
        partial.replace(dest)
    finally:
        partial.unlink(missing_ok=True)

def self_test(items):
    # No remote access. Exercise positive and negative ZIP digest checks.
    with tempfile.TemporaryDirectory() as td:
        archive = Path(td) / "probe.zip"
        payload = b"locked immutable MiniAS fixture test"
        inner_hash = hashlib.sha256(payload).hexdigest()
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_STORED) as out:
            out.writestr("corpus.tar.gz", payload)
        record = {
            "zip_sha256": sha256_file(archive),
            "files": {"corpus.tar.gz": inner_hash},
        }
        verify_zip(archive, "probe", record)
        try:
            verify_zip(archive, "probe", {**record, "zip_sha256": "0" * 64})
        except AssertionError:
            pass
        else:
            raise AssertionError("corrupted external archive not rejected")
        try:
            verify_zip(archive, "probe", {**record,
                                         "files": {"corpus.tar.gz": "0" * 64}})
        except AssertionError:
            pass
        else:
            raise AssertionError("corrupted inner payload not rejected")
    print(f"MINIAS_ARTIFACT_SNAPSHOT=PASS artifacts={len(items)} zip_pins=9 inner_pins=8 negatives=2")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--self-test", action="store_true")
    modes.add_argument("--download", action="store_true")
    modes.add_argument("--verify-dir", type=Path)
    parser.add_argument("--output-dir", type=Path,
                        help="Required for --download; saves original unmodified ZIPs")
    parser.add_argument("--names", nargs="+",
                        help="Optional artifact subset (default: all nine)")
    args = parser.parse_args()
    data = json.loads(CATALOG.read_text())
    items = check_catalog(data)
    if args.self_test:
        self_test(items)
        return
    names = args.names or sorted(items)
    assert len(set(names)) == len(names)
    assert set(names) <= set(items), f"unknown artifact: {set(names) - set(items)}"
    if args.download:
        assert args.output_dir is not None
        directory = args.output_dir
        directory.mkdir(parents=True, exist_ok=True)
        token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    else:
        assert args.output_dir is None
        directory = args.verify_dir
    for name in names:
        path = directory / (name + ".zip")
        if args.download and not path.exists():
            download_artifact(data["repository"], name, items[name], path, token)
        digest = verify_zip(path, name, items[name])
        print(f"MINIAS_ARTIFACT_VERIFIED name={name} sha256={digest}")
    print(f"MINIAS_ARTIFACT_SET=PASS checked={len(names)} directory={directory}")

if __name__ == "__main__":
    main()
