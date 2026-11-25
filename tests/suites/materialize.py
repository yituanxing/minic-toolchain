#!/usr/bin/env python3
"""Readable test case suites. Each named case remains an independent input.
The checked-in suite is authoritative; expanded legacy paths are generated.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SUITES = ROOT / "tests/suites"
MARK = b"@@@ "
END = b"@@@ end\n"

def records(path):
    with path.open("rb") as f:
        for header in f:
            if not header.startswith(MARK) or header == END:
                raise ValueError(f"invalid suite header in {path}: {header!r}")
            meta = json.loads(header[len(MARK):])
            body = bytearray()
            for line in f:
                if line == END:
                    break
                if line.startswith(MARK):
                    raise ValueError(f"unclosed section in {path}")
                body.extend(line)
            else:
                raise ValueError(f"missing end marker in {path}")
            if not meta["newline"]:
                if not body.endswith(b"\n"):
                    raise ValueError("missing framing newline")
                del body[-1:]
            yield meta, bytes(body)

def inventory():
    outputs = {}
    for suite in sorted(SUITES.glob("*.cases")):
        prefixes = {}
        for meta, body in records(suite):
            if meta["kind"] == "shared":
                ident = meta["id"]
                if ident in prefixes:
                    raise ValueError(f"duplicate shared block {ident}")
                prefixes[ident] = body
                continue
            if meta["kind"] != "case":
                raise ValueError("unknown record")
            p = PurePosixPath(meta["path"])
            if p.is_absolute() or ".." in p.parts or p.parts[0] != "tests" or p.parts[1] in ("suites", "vendor"):
                raise ValueError(f"unsafe output path {p}")
            name = str(p)
            if name in outputs:
                raise ValueError(f"duplicate case path {name}")
            if "shared" in meta:
                lines = body.splitlines(keepends=True)
                n = meta["head_lines"]
                if not isinstance(n, int) or not 0 <= n <= len(lines):
                    raise ValueError("invalid shared insertion")
                body = b"".join(lines[:n]) + prefixes[meta["shared"]] + b"".join(lines[n:])
            mode = meta["mode"]
            if mode not in ("100644", "100755"):
                raise ValueError("unsupported file mode")
            outputs[name] = (body, int(mode[-3:], 8))
    return outputs

def materialize(refresh=False):
    outputs = inventory()
    lockdir = ROOT / "build"
    lockdir.mkdir(exist_ok=True)
    with (lockdir / "test-suite.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        tracked = set(subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0"))
        for name, (body, mode) in outputs.items():
            p = ROOT / name
            if name in tracked:
                raise ValueError(f"suite output collides with tracked file: {name}")
            if p.is_symlink() or any(q.is_symlink() for q in p.parents if q != ROOT):
                raise ValueError(f"symlink in output path {name}")
            if p.exists():
                if p.read_bytes() == body and p.stat().st_mode & 0o777 == mode:
                    continue
                if not refresh:
                    raise ValueError(f"generated test differs: {name}; edit suite or use --refresh")
            p.parent.mkdir(parents=True, exist_ok=True)
            fd, tmp = tempfile.mkstemp(prefix=".case-", dir=p.parent)
            try:
                with os.fdopen(fd, "wb") as f:
                    f.write(body)
                os.chmod(tmp, mode)
                os.replace(tmp, p)
            finally:
                if os.path.exists(tmp):
                    os.unlink(tmp)
    return len(outputs)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--list", action="store_true")
    p.add_argument("--refresh", action="store_true", help="replace generated copies after editing a canonical suite")
    args = p.parse_args()
    if args.list:
        print("\n".join(inventory()))
    else:
        print(f"TEST_SUITE_MATERIALIZED={materialize(args.refresh)}")

if __name__ == "__main__":
    main()
