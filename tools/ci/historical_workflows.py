#!/usr/bin/env python3
"""Restore immutable, disabled YAMLs for historical CI integrity checks only."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SNAPSHOT = "cd8786f4341c9de1049fe839fe37392b01faaa4a"
PREFIX = ".github/workflows-disabled/"
COUNT = 68


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def inventory():
    rows = {}
    for row in git("ls-tree", "-rz", SNAPSHOT, "--", PREFIX).split(b"\0"):
        if not row:
            continue
        meta, raw_path = row.split(b"\t", 1)
        mode, kind, sha = meta.decode().split()
        path = raw_path.decode()
        if not path.endswith(".yml"):
            continue
        if kind != "blob" or mode != "100644" or not path.startswith(PREFIX):
            raise RuntimeError("unexpected historical entry: " + path)
        rows[path] = sha
    if len(rows) != COUNT:
        raise RuntimeError("historical workflow inventory mismatch")
    return rows


def blob_hash(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def restore():
    rows = inventory()
    # Preflight all destinations before writing any file. Never overwrite
    # user changes or put historical workflows under the executable folder.
    for path in rows:
        dest = ROOT / path
        if dest.exists() or dest.is_symlink():
            raise RuntimeError("historical destination already exists: " + path)
    created = []
    try:
        for path, sha in rows.items():
            data = git("cat-file", "blob", sha)
            if blob_hash(data) != sha:
                raise RuntimeError("historical blob mismatch: " + path)
            dest = ROOT / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            with dest.open("xb") as stream:
                created.append(dest)
                stream.write(data)
    except BaseException:
        for dest in reversed(created):
            dest.unlink()
        raise
    print(f"HISTORICAL_WORKFLOWS_RESTORED count={len(rows)} snapshot={SNAPSHOT}")


def clean():
    rows = inventory()
    # Refuse to erase modified or tracked files.
    tracked = set(git("ls-files", "-z", "--", PREFIX).decode().split("\0"))
    existing = []
    for path, sha in rows.items():
        dest = ROOT / path
        if not dest.exists() and not dest.is_symlink():
            continue
        if path in tracked or dest.is_symlink() or blob_hash(dest.read_bytes()) != sha:
            raise RuntimeError("refusing to remove changed/tracked archive: " + path)
        existing.append(dest)
    for dest in existing:
        dest.unlink()
    print(f"HISTORICAL_WORKFLOWS_CLEANED count={len(existing)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["restore", "clean", "run"])
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.mode != "run":
        if args.command:
            parser.error("unexpected command")
        (restore if args.mode == "restore" else clean)()
        return 0
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("run requires a command")
    restore()
    try:
        return subprocess.run(command, cwd=ROOT).returncode
    finally:
        clean()


if __name__ == "__main__":
    sys.exit(main())
