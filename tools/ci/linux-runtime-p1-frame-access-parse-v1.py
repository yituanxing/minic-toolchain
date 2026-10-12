#!/usr/bin/env python3
"""Validate first real access in each safely block-local MiniC scalar object."""
import csv
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

RX = re.compile(r"^CORE_FRAME_ACCESS function=(\S+) id=(\d+) first=(READ|WRITE|NONE)$")


def parse(log: str, intervals: str) -> str:
    expected = set()
    for row in csv.DictReader(intervals.splitlines(), delimiter="\t"):
        if row["reusable"] == "1":
            expected.add((row["function"], int(row["id"])))
    actual = {}
    for line in log.splitlines():
        if hit := RX.fullmatch(line):
            name, idx, first = hit.groups()
            key = name, int(idx)
            if key in actual:
                raise ValueError(f"duplicate access {key}")
            actual[key] = first
    if expected != actual.keys():
        raise ValueError(
            f"first access incomplete missing={sorted(expected-actual.keys())[:10]} "
            f"extra={sorted(actual.keys()-expected)[:10]}"
        )
    rows = ["function\tid\tfirst_access\tdef_before_read"]
    stats = defaultdict(Counter)
    for (name, idx), first in sorted(actual.items()):
        stats[name][first] += 1
        rows.append(f"{name}\t{idx}\t{first}\t{int(first=='WRITE')}")
    for name in sorted(stats):
        values = stats[name]
        print(f"P1_FRAME_FIRST_ACCESS=PASS function={name} "
              f"write_first={values['WRITE']} "
              f"read_first={values['READ']} "
              f"no_access={values['NONE']}")
    return "\n".join(rows) + "\n"


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        intervals = ("function\tid\tphysical_offset\treusable\tblock\tfirst\tlast\tshares_start\n"
                     "f\t0\t0\t1\t3\t0\t2\t0\n")
        raw = "CORE_FRAME_ACCESS function=f id=0 first=WRITE\n"
        assert "\tf\t" not in parse(raw, intervals)
        assert "f\t0\tWRITE\t1" in parse(raw, intervals)
        try:
            parse(raw.replace("first=WRITE", "first=NONE"), intervals + "f\t1\t8\t1\t4\t0\t2\t0\n")
        except ValueError:
            pass
        else:
            raise AssertionError("accepted missing access")
        print("P1_FRAME_FIRST_ACCESS_SELFTEST=PASS")
    elif len(sys.argv) == 4:
        log, intervals, dest = map(Path, sys.argv[1:])
        dest.write_text(parse(log.read_text(errors="replace"), intervals.read_text()))
    else:
        raise SystemExit("usage: first-access.py <replay.log> <intervals.tsv> <first-access.tsv> | --self-test")
