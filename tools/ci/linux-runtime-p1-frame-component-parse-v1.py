#!/usr/bin/env python3
"""Parse stderr-only MiniC RV64 CoreObject frame audit, validating exact totals."""
from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path

HEADER = re.compile(
    r"^CORE_FRAME function=(\S+) frame=(\d+) value_bytes=(\d+) "
    r"values=(\d+) objects=(\d+)$"
)
COMPONENT = re.compile(
    r"^CORE_FRAME_COMPONENT function=(\S+) frame=(\d+) "
    r"outgoing=(\d+) object_region=(\d+) value_bytes=(\d+) "
    r"suffix=(\d+) object_payload=(\d+) unique_offsets=(\d+) "
    r"shared_offsets=(\d+)$"
)
OBJECT = re.compile(
    r"^CORE_FRAME_OBJECT function=(\S+) id=(\d+) offset=(\d+) "
    r"element_bytes=(\d+) elements=(\d+) align=(\d+) "
    r"explicit_align=(\d+) kind=(\w+) shares_start=([01])$"
)

def analyze(raw: str) -> tuple[str, str]:
    headers: dict[str, tuple[int, int, int, int]] = {}
    components: dict[str, tuple[int, ...]] = {}
    objects: dict[str, list[tuple[str, ...]]] = defaultdict(list)
    # Main tuple storage uses strings to preserve exact evidence fields.
    for line in raw.splitlines():
        if m := HEADER.fullmatch(line):
            name = m.group(1)
            if name in headers:
                raise ValueError("duplicate header: " + name)
            headers[name] = tuple(map(int, m.groups()[1:]))
        elif m := COMPONENT.fullmatch(line):
            name = m.group(1)
            if name in components:
                raise ValueError("duplicate component: " + name)
            components[name] = tuple(map(int, m.groups()[1:]))
        elif m := OBJECT.fullmatch(line):
            name = m.group(1)
            objects[name].append(m.groups()[1:])
    if not headers or headers.keys() != components.keys():
        raise ValueError("missing/unpaired CORE_FRAME and CORE_FRAME_COMPONENT records")
    component_rows = [
        "function\tframe_bytes\toutgoing_bytes\tobject_region_bytes\tvalue_bytes\t"
        "suffix_bytes\tobject_payload_bytes\tunique_offsets\tshared_offsets\tobjects"
    ]
    object_rows = [
        "function\tid\toffset\telement_bytes\telements\talign\texplicit_align\tkind\tshares_start"
    ]
    for name in sorted(headers):
        frame, value_bytes, _, count = headers[name]
        cframe, outgoing, region, cval, suffix, payload, unique, shared = components[name]
        if frame != cframe or value_bytes != cval or frame != outgoing + region + cval + suffix:
            raise ValueError(f"frame accounting mismatch: {name}")
        if count != len(objects[name]) or count != unique + shared:
            raise ValueError(f"CoreObject count mismatch: {name}")
        seen = set()
        shared_actual = 0
        for row in objects[name]:
            obj_id, offset, element, elements, align, expl, shape, share = row
            i, pos = int(obj_id), int(offset)
            if i in seen or i >= count or pos >= frame or int(align) == 0:
                raise ValueError(f"invalid CoreObject descriptor: {name} object={obj_id}")
            if share == "1":
                shared_actual += 1
            seen.add(i)
            object_rows.append(name + "\t" + "\t".join(row))
        if seen != set(range(count)) or shared_actual != shared:
            raise ValueError(f"CoreObject coverage/overlap mismatch: {name}")
        component_rows.append(
            "\t".join(map(str, (name, frame, outgoing, region, cval, suffix,
                                payload, unique, shared, count)))
        )
        print(f"P1_FRAME_COMPONENT=PASS function={name} frame={frame} "
              f"object_region={region} value_bytes={cval} "
              f"objects={count} shared_starts={shared}")
    return "\n".join(component_rows) + "\n", "\n".join(object_rows) + "\n"


def self_test() -> None:
    raw = (
        "CORE_FRAME function=sample frame=64 value_bytes=8 values=2 objects=2\n"
        "CORE_FRAME_OBJECT function=sample id=0 offset=0 element_bytes=8 "
        "elements=1 align=8 explicit_align=0 kind=scalar shares_start=0\n"
        "CORE_FRAME_OBJECT function=sample id=1 offset=0 element_bytes=8 "
        "elements=1 align=8 explicit_align=0 kind=scalar shares_start=1\n"
        "CORE_FRAME_COMPONENT function=sample frame=64 outgoing=0 "
        "object_region=24 value_bytes=8 suffix=32 object_payload=16 "
        "unique_offsets=1 shared_offsets=1\n"
    )
    component, objects = analyze(raw)
    assert "sample\t64\t0\t24\t8\t32" in component
    assert len(objects.splitlines()) == 3
    try:
        analyze(raw.replace("suffix=32", "suffix=31"))
    except ValueError:
        pass
    else:
        raise AssertionError("incorrect frame size accepted")
    print("P1_FRAME_COMPONENT_PARSER_SELFTEST=PASS")


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        self_test()
    elif len(sys.argv) == 4:
        src, comps, objects = map(Path, sys.argv[1:])
        comp_text, object_text = analyze(src.read_text(errors="replace"))
        comps.write_text(comp_text)
        objects.write_text(object_text)
    else:
        raise SystemExit("usage: parse.py <audit.log> <components.tsv> <objects.tsv> | --self-test")
