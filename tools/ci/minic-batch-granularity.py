#!/usr/bin/env python3
"""Auditable MiniC batch size, distinguishing implementation and evidence.

The report is not a substitute for a passing GNU/QEMU or frozen-kernel gate.
Use --strict for a planned feature batch; small safety hotfixes may explicitly
remain report-only instead of being forced into unrelated changes.
"""

import argparse
from pathlib import Path
import subprocess
import sys


SOURCE = ("src/", "include/", "tools/")
EVIDENCE = ("tests/",)
WORKFLOW = (".github/workflows/",)
DOCS = ("docs/",)
NON_CODE_SUFFIXES = frozenset((
    ".md", ".rst", ".txt", ".json", ".lock", ".csv", ".tsv",
    ".png", ".jpg", ".jpeg", ".svg", ".pdf", ".log", ".o", ".a",
    ".so", ".elf", ".bin", ".map", ".srec", ".hex", ".patch",
))


def run_git(*args):
    result = subprocess.run(
        ("git", *args), check=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout


def parse_numstat(payload):
    """Parse NUL-delimited git numstat, including rename/copy source paths.

    -z uses a special three-NUL form for renames; taking the final pathname
    ensures a test moved into src/ is classified by its destination.
    Binary numstat entries use '-' and do not masquerade as code lines.
    """
    entries = []
    fields = payload.split(b"\0")
    pos = 0
    while pos < len(fields):
        record = fields[pos]
        pos += 1
        if not record:
            continue
        pieces = record.split(b"\t", 2)
        if len(pieces) != 3:
            raise ValueError("unexpected git --numstat -z output")
        added, deleted, path = pieces
        if not path:
            if pos + 1 >= len(fields):
                raise ValueError("incomplete git rename numstat entry")
            # fields[pos] holds the old path; fields[pos+1] the new path.
            path = fields[pos + 1]
            pos += 2
        if added == b"-" or deleted == b"-":
            continue
        entries.append((
            path.decode("utf-8", "surrogateescape"),
            int(added), int(deleted),
        ))
    return entries


def classify(path):
    if path.startswith(DOCS) or path.endswith((".md", ".rst")):
        return "documentation"
    if path.startswith(WORKFLOW) or path.startswith(".github/"):
        return "workflow"
    if path.endswith(tuple(NON_CODE_SUFFIXES)):
        return "other"
    if path.startswith(SOURCE):
        return "implementation"
    if path.startswith(EVIDENCE):
        return "verification"
    return "other"


def untracked_entries():
    output = run_git("ls-files", "--others", "--exclude-standard", "-z")
    result = []
    for raw in output.split(b"\0"):
        if not raw:
            continue
        path = raw.decode("utf-8", "surrogateescape")
        if classify(path) == "other":
            continue
        try:
            content = Path(path).read_bytes()
        except OSError:
            continue
        if b"\0" in content:
            continue
        # Git counts a final non-newline-terminated line as a line too.
        result.append((path, content.count(b"\n") +
                       int(bool(content) and not content.endswith(b"\n")), 0))
    return result


def audit(entries, minimum, maximum):
    totals = {
        "implementation": [0, 0],
        "verification": [0, 0],
        "workflow": [0, 0],
        "documentation": [0, 0],
        "other": [0, 0],
    }
    for path, added, deleted in entries:
        kind = classify(path)
        totals[kind][0] += added
        totals[kind][1] += deleted
    effective = sum(sum(totals[kind]) for kind in
                    ("implementation", "verification"))
    gross = sum(sum(pair) for pair in totals.values())
    print("MINIC_BATCH_GRANULARITY", f"effective={effective}",
          f"gross={gross}", f"range={minimum}..{maximum}")
    for kind, (added, deleted) in totals.items():
        print(f"  {kind}: +{added} -{deleted} changed={added + deleted}")
    if effective < minimum:
        result = "BELOW_TARGET"
    elif effective > maximum:
        result = "ABOVE_TARGET"
    else:
        result = "WITHIN_TARGET"
    print(f"  result={result}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="HEAD", help="Git base commit")
    parser.add_argument("--head", help="Optional comparison commit")
    parser.add_argument("--worktree", action="store_true",
                        help="Count all local tracked and untracked edits")
    parser.add_argument("--minimum", type=int, default=800)
    parser.add_argument("--maximum", type=int, default=2000)
    parser.add_argument("--strict", action="store_true",
                        help="Fail for a feature batch outside the range")
    args = parser.parse_args()
    if args.minimum < 1 or args.maximum < args.minimum:
        parser.error("minimum must be positive and <= maximum")
    if args.worktree and args.head:
        parser.error("--head and --worktree are mutually exclusive")
    try:
        diff_args = ("diff", "--no-ext-diff", "--numstat", "--find-renames",
                     "-z", args.base)
        if args.head:
            diff_args += (args.head,)
        entries = parse_numstat(run_git(*diff_args))
        if args.worktree:
            entries += untracked_entries()
        result = audit(entries, args.minimum, args.maximum)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"MINIC_BATCH_GRANULARITY=ERROR {exc}", file=sys.stderr)
        return 1
    return 2 if args.strict and result != "WITHIN_TARGET" else 0


if __name__ == "__main__":
    sys.exit(main())
