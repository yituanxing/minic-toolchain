#!/usr/bin/env python3
"""Summarize discarded .exitcall.exit inputs from a GNU BFD linker map.

This is evidence of input-section disposition, *not* a count of GNU BFD
dynamic-relocation reservations.  In particular, an excluded input may have
had its relocation capacity reserved earlier in BFD's target-specific passes.
"""

from pathlib import Path
import re
import sys


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: minild-gnu-link-map-discard-report.py <GNU-map>")

    path = Path(sys.argv[1])
    section_line = re.compile(r"^\s*\.exitcall\.exit(?=\s|$)(.*)$")
    placement = re.compile(
        r"\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\S+)"
    )
    in_discard = False
    found_discard = False
    finished_discard = False
    pending = False
    records: list[tuple[str, str, str]] = []
    unresolved: list[str] = []

    with path.open(encoding="utf-8", errors="replace") as stream:
        for raw in stream:
            stripped = raw.strip()
            if stripped == "Discarded input sections":
                in_discard = True
                found_discard = True
                continue
            if in_discard and stripped == "Memory Configuration":
                finished_discard = True
                break
            if not in_discard:
                continue

            if pending:
                match = placement.match(raw)
                if match is not None:
                    records.append(match.groups())
                else:
                    unresolved.append(raw.rstrip("\n"))
                pending = False
                continue

            match = section_line.match(raw)
            if match is None:
                continue
            placed = placement.match(match.group(1))
            if placed is not None:
                records.append(placed.groups())
            else:
                pending = True

    if pending:
        unresolved.append("<eof following .exitcall.exit section header>")
    if not found_discard or not finished_discard:
        raise SystemExit("GNU_MAP=FAIL unrecognized_discarded_input_section_region")
    if not records:
        raise SystemExit("GNU_MAP=FAIL no_discarded_exitcall_sources")
    if unresolved:
        raise SystemExit(
            "GNU_MAP=FAIL unresolved_exitcall_section_sources="
            + str(len(unresolved))
            + ":"
            + repr(unresolved[:5])
        )

    print(f"GNU BFD map: {path}")
    print("Discarded input section: .exitcall.exit")
    print("Address Size Original_Input_Source")
    for address, size, source in records:
        print(f"{address} {size} {source}")
    print(f"GNU_MAP_DISCARDED_EXITCALL={len(records)} SOURCE_RESOLVED={len(records)}")


if __name__ == "__main__":
    main()
