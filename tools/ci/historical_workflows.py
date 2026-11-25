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



# The former job bodies are preserved as evidence, not executable current CI.
# Verify the live workflow against the *entire* pinned original with exactly
# four deterministic fail-closed substitutions before exposing historical text
# to the original archive/hash guards. Unrelated job bodies and routing remain
# byte-for-byte protected.
RETIRED_JOB_SNAPSHOT = "fe0a83481864b2808f62098510b08df579ff42a5"
RETIRED_JOBS = {
    ".github/workflows/linux-expanded-pi-p1-runtime-v1.yml": {"pi-runtime"},
    ".github/workflows/linux-runtime-focused-faults-v1.yml": {"cpu-stall", "fork-stack", "candidate"},
}
RETIRED_JOB_STEPS = (
    "      - name: Reject retired pre-split compiler overlay\n"
    "        shell: bash\n"
    "        run: |\n"
    "          echo 'HISTORICAL_PROFILE_RETIRED: exact pre-split overlay replay requires historical checkout eb583f9dd9bf9cb2ebf530a8579930311b8539d9; no current compiler or runtime PASS.' >&2\n"
    "          exit 78\n"
)


def historical_job_view(path):
    """Validate live retirement, then return original for historical hash checks."""
    import re
    path = Path(path)
    relative = path.resolve().relative_to(ROOT).as_posix()
    current = path.read_text()
    if relative not in RETIRED_JOBS:
        return current
    original = git("show", RETIRED_JOB_SNAPSHOT + ":" + relative).decode()
    marks = list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):[ \t]*$", original))
    marks = [m for m in marks if m.start() > original.index("\njobs:\n")]
    expected = original
    replaced = set()
    for i in range(len(marks) - 1, -1, -1):
        mark = marks[i]
        if mark.group(1) not in RETIRED_JOBS[relative]:
            continue
        end = marks[i + 1].start() if i + 1 < len(marks) else len(original)
        body = original[mark.start():end]
        prefix, separator, _ = body.partition("    steps:\n")
        if not separator:
            raise RuntimeError("missing original job steps: " + mark.group(1))
        expected = (expected[:mark.start()] + prefix + separator + RETIRED_JOB_STEPS
                    + ("\n" if end < len(original) else "") + expected[end:])
        replaced.add(mark.group(1))
    if replaced != RETIRED_JOBS[relative] or current != expected:
        raise RuntimeError("unexpected live historical job retirement drift: " + relative)
    return original


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
