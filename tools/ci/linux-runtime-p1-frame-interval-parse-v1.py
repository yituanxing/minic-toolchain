#!/usr/bin/env python3
"""Validate actual CoreScalarObjectInterval block assignments in frozen MiniC."""
from __future__ import annotations

import csv
import re
import sys
from collections import defaultdict
from pathlib import Path

PATTERN = re.compile(
    r"^CORE_FRAME_INTERVAL function=(\S+) id=(\d+) reusable=([01]) "
    r"block=(\d+) first=(\d+) last=(\d+)$"
)


def parse(raw: str, objects_data: str) -> tuple[str, dict[str, tuple[int, int, int, int]]]:
    objects: dict[tuple[str, int], dict[str, str]] = {}
    for row in csv.DictReader(objects_data.splitlines(), delimiter="\t"):
        key = (row["function"], int(row["id"]))
        if key in objects:
            raise ValueError("duplicate object " + str(key))
        objects[key] = row
    intervals: dict[tuple[str, int], tuple[int, int, int, int]] = {}
    for line in raw.splitlines():
        if match := PATTERN.fullmatch(line):
            name, idx, reuse, block, first, last = match.groups()
            key = name, int(idx)
            if key in intervals:
                raise ValueError("duplicate CoreObject interval " + str(key))
            intervals[key] = tuple(map(int, (reuse, block, first, last)))
    if not objects or objects.keys() != intervals.keys():
        missing = sorted(objects.keys() - intervals.keys())[:10]
        extra = sorted(intervals.keys() - objects.keys())[:10]
        raise ValueError(f"CoreObject interval coverage failed missing={missing} extra={extra}")
    output = ["function\tid\tphysical_offset\treusable\tblock\tfirst\tlast\tshares_start"]
    stats: dict[str, list[tuple[int, int, int, int]]] = defaultdict(list)
    for name, idx in sorted(intervals):
        reuse, block, first, last = intervals[name, idx]
        if reuse and first > last:
            raise ValueError(f"invalid ordered interval {name} {idx}")
        data = objects[name, idx]
        output.append("\t".join(map(str, (
            name, idx, data["offset"], reuse,
            block if reuse else "NOT_REUSABLE",
            first if reuse else "-",
            last if reuse else "-",
            data["shares_start"],
        ))))
        stats[name].append((reuse, block, first, last))
    for name in sorted(stats):
        life = stats[name]
        reusable = [t for t in life if t[0] == 1]
        blocks = {t[1] for t in reusable}
        offsets = {int(objects[name, idx]["offset"])
                   for key_name, idx in intervals if key_name == name
                   and intervals[key_name, idx][0] == 1}
        print(f"P1_FRAME_INTERVAL=PASS function={name} objects={len(life)} "
              f"reusable={len(reusable)} unique_owner_blocks={len(blocks)} "
              f"reusable_unique_stack_offsets={len(offsets)}")
    return "\n".join(output) + "\n", {
        name: (
            len(records),
            sum(x[0] for x in records),
            len({x[1] for x in records if x[0]}),
            len({int(objects[name, idx]["offset"])
                 for key_name, idx in intervals if key_name == name
                 and intervals[key_name, idx][0]}),
        ) for name, records in stats.items()
    }


def self_test() -> None:
    example = "CORE_FRAME_INTERVAL function=f id=0 reusable=1 block=1 first=2 last=5\n"
    objects = (
        "function\tid\toffset\telement_bytes\telements\talign\t"
        "explicit_align\tkind\tshares_start\n"
        "f\t0\t8\t8\t1\t8\t0\tscalar\t0\n"
    )
    rows, stats = parse(example, objects)
    assert "f\t0\t8\t1\t1\t2\t5" in rows
    assert stats["f"] == (1, 1, 1, 1)
    try:
        parse(example.replace("last=5", "last=1"), objects)
    except ValueError:
        pass
    else:
        raise AssertionError("accepted inverted interval")
    print("P1_FRAME_INTERVAL_PARSER_SELFTEST=PASS")


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        self_test()
    elif len(sys.argv) == 4:
        log, objects, dest = map(Path, sys.argv[1:])
        rows, _ = parse(log.read_text(errors="replace"), objects.read_text())
        dest.write_text(rows)
    else:
        raise SystemExit("usage: intervals.py replay.log objects.tsv intervals.tsv | --self-test")
