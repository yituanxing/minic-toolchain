#!/usr/bin/env python3
import argparse
import bisect
import json
import re
from pathlib import Path


FAULT_RE = re.compile(
    r"epc:0x([0-9a-fA-F]+).*tval:0x([0-9a-fA-F]+).*desc=(\S+)"
)


def parse_int(value):
    if isinstance(value, int):
        return value
    return int(str(value), 0)


def load_symbols(path: Path):
    symbols = []
    by_name = {}
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        try:
            address = int(parts[0], 16)
        except ValueError:
            continue
        name = parts[2]
        symbols.append((address, name))
        by_name.setdefault(name, address)
    symbols.sort()
    return symbols, [address for address, _ in symbols], by_name


def resolve(symbols, addresses, address):
    index = bisect.bisect_right(addresses, address) - 1
    if index < 0:
        return "?", 0
    base, name = symbols[index]
    return name, address - base


def is_expected_mmu_transition_fault(fault):
    return (
        fault["desc"] == "exec_page_fault"
        and fault["relocated"]
        and fault["tval"] == fault["epc"]
        and fault["symbol"] == "relocate_enable_mmu"
        and fault["offset"] == 0x48
    )


def parse_faults(text, symbols, addresses, phys_entry, relocation_delta):
    faults = []
    for match in FAULT_RE.finditer(text):
        desc = match.group(3)
        if "fault" not in desc:
            continue
        epc = int(match.group(1), 16)
        tval = int(match.group(2), 16)
        lookup = epc
        relocated = False
        if relocation_delta is not None and epc >= phys_entry:
            linked_floor = phys_entry + relocation_delta
            if epc < linked_floor:
                lookup = epc + relocation_delta
                relocated = True
        symbol, offset = resolve(symbols, addresses, lookup)
        faults.append(
            {
                "epc": epc,
                "tval": tval,
                "desc": desc,
                "lookup": lookup,
                "relocated": relocated,
                "symbol": symbol,
                "offset": offset,
                "match_start": match.start(),
            }
        )
    return faults


def address_candidates(by_name, name, relocation_delta):
    address = by_name.get(name)
    if address is None:
        return []
    candidates = [address]
    if relocation_delta is not None and address >= relocation_delta:
        candidates.append(address - relocation_delta)
    return candidates


def symbol_executed(trace_prefix, by_name, name, relocation_delta):
    for address in address_candidates(by_name, name, relocation_delta):
        if f"0x{address:x}:" in trace_prefix or f"0x{address:016x}:" in trace_prefix:
            return True
    return False


def normalized_fault(fault):
    if fault is None:
        return None
    return {
        "desc": fault["desc"],
        "symbol": fault["symbol"],
        "offset": fault["offset"],
    }


def same_fault(actual, expected):
    if actual is None or expected is None:
        return False
    return (
        actual["desc"] == expected["desc"]
        and actual["symbol"] == expected["symbol"]
        and actual["offset"] == parse_int(expected["offset"])
    )


def classify_observation(observation, config):
    expected = config["baseline"]["fault"]
    baseline_marker = config["baseline"]["progress_marker"]
    marker_names = [marker["name"] for marker in config["progress_markers"]]
    if baseline_marker not in marker_names:
        raise ValueError(f"baseline progress marker not present: {baseline_marker}")
    baseline_rank = marker_names.index(baseline_marker)

    fault = observation.get("fault")
    progress_rank = observation.get("progress_rank", -1)
    pass_hit = bool(observation.get("pass_marker_hit"))

    if fault is not None:
        if same_fault(fault, expected):
            return "SAME_FAULT", "normalized first fault matches certified baseline"
        if progress_rank > baseline_rank:
            return "MOVED_LATER", "different first fault after a later certified progress marker"
        if progress_rank < baseline_rank:
            return "REGRESSED", "different first fault before the certified baseline progress marker"
        return "INCONCLUSIVE", "different first fault without proof that execution moved later"

    if pass_hit:
        return "FRONTIER_PASS", "explicit frontier pass marker reached with no unexpected kernel fault"
    if progress_rank > baseline_rank:
        return "MOVED_LATER", "no unexpected fault and execution reached a later progress marker"
    return "INCONCLUSIVE", "no unexpected fault, but no explicit proof of forward progress"


def run_self_test():
    config = {
        "baseline": {
            "fault": {"desc": "fault_load", "symbol": "strlen", "offset": "0x6"},
            "progress_marker": "fdt-path",
        },
        "progress_markers": [
            {"name": "entry", "symbol": "_start"},
            {"name": "fdt-path", "symbol": "__pi_fdt_path_offset"},
            {"name": "later", "symbol": "start_kernel"},
        ],
    }
    cases = [
        (
            "same",
            {
                "fault": {"desc": "fault_load", "symbol": "strlen", "offset": 6},
                "progress_rank": 1,
                "pass_marker_hit": False,
            },
            "SAME_FAULT",
        ),
        (
            "moved",
            {
                "fault": {"desc": "fault_load", "symbol": "foo", "offset": 4},
                "progress_rank": 2,
                "pass_marker_hit": False,
            },
            "MOVED_LATER",
        ),
        (
            "regressed",
            {
                "fault": {"desc": "fault_load", "symbol": "foo", "offset": 4},
                "progress_rank": 0,
                "pass_marker_hit": False,
            },
            "REGRESSED",
        ),
        (
            "timeout-no-proof",
            {"fault": None, "progress_rank": 1, "pass_marker_hit": False},
            "INCONCLUSIVE",
        ),
        (
            "explicit-pass",
            {"fault": None, "progress_rank": 1, "pass_marker_hit": True},
            "FRONTIER_PASS",
        ),
    ]
    for name, observation, expected in cases:
        verdict, _ = classify_observation(observation, config)
        if verdict != expected:
            raise SystemExit(
                f"SELF_TEST_FAIL case={name} expected={expected} actual={verdict}"
            )
        print(f"SELF_TEST_PASS case={name} verdict={verdict}")
    print("SELF_TEST_PASS total=5")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Classify Linux runtime progress against a certified first-fault frontier."
    )
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--config", type=Path)
    parser.add_argument("--interrupts", type=Path)
    parser.add_argument("--nm", type=Path)
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--console", type=Path)
    parser.add_argument("--qemu-rc")
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--text-out", type=Path)
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()

    required = [
        args.config,
        args.interrupts,
        args.nm,
        args.trace,
        args.console,
        args.json_out,
        args.text_out,
    ]
    if any(value is None for value in required) or args.qemu_rc is None:
        parser.error("runtime classification requires config/interrupts/nm/trace/console/qemu-rc/json-out/text-out")

    config = json.loads(args.config.read_text())
    phys_entry = parse_int(config.get("phys_entry", "0x80200000"))

    symbols, addresses, by_name = load_symbols(args.nm)
    anchor_name = None
    anchor_address = None
    for candidate in ("_start", "_text", "_stext"):
        if candidate in by_name:
            anchor_name = candidate
            anchor_address = by_name[candidate]
            break

    relocation_delta = None
    if anchor_address is not None and anchor_address > phys_entry:
        relocation_delta = anchor_address - phys_entry

    trace = args.trace.read_text(errors="replace")
    console = args.console.read_text(errors="replace")
    interrupts = args.interrupts.read_text(errors="replace")

    faults = parse_faults(interrupts, symbols, addresses, phys_entry, relocation_delta)
    kernel_faults = [fault for fault in faults if fault["epc"] >= phys_entry]
    unexpected = [fault for fault in kernel_faults if not is_expected_mmu_transition_fault(fault)]
    first_fault = unexpected[0] if unexpected else None

    trace_cut = len(trace)
    if first_fault is not None:
        for match in FAULT_RE.finditer(trace):
            epc = int(match.group(1), 16)
            tval = int(match.group(2), 16)
            desc = match.group(3)
            if epc == first_fault["epc"] and tval == first_fault["tval"] and desc == first_fault["desc"]:
                trace_cut = match.start()
                break
    trace_prefix = trace[:trace_cut]

    reached = []
    progress_rank = -1
    for index, marker in enumerate(config["progress_markers"]):
        hit = symbol_executed(
            trace_prefix, by_name, marker["symbol"], relocation_delta
        )
        if hit:
            progress_rank = index
            reached.append(marker["name"])

    pass_hits = []
    for marker in config.get("pass_markers", {}).get("console_any", []):
        if marker in console:
            pass_hits.append(f"console:{marker}")
    for marker in config.get("pass_markers", {}).get("symbol_any", []):
        if symbol_executed(trace, by_name, marker, relocation_delta):
            pass_hits.append(f"symbol:{marker}")

    observation = {
        "fault": normalized_fault(first_fault),
        "progress_rank": progress_rank,
        "progress_reached": reached,
        "pass_marker_hit": bool(pass_hits),
        "pass_markers": pass_hits,
    }
    verdict, reason = classify_observation(observation, config)

    result = {
        "schema": 1,
        "frontier": config.get("name", "linux-runtime"),
        "verdict": verdict,
        "reason": reason,
        "qemu_rc": int(args.qemu_rc),
        "fault": normalized_fault(first_fault),
        "fault_detail": first_fault,
        "progress_rank": progress_rank,
        "progress_reached": reached,
        "highest_progress": reached[-1] if reached else None,
        "pass_markers": pass_hits,
        "unexpected_kernel_faults": len(unexpected),
        "kernel_faults": len(kernel_faults),
        "link_anchor": (
            {"name": anchor_name, "address": anchor_address}
            if anchor_name is not None
            else None
        ),
        "relocation_delta": relocation_delta,
    }

    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.text_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")

    fault_text = "none"
    if result["fault"]:
        fault_text = (
            f'{result["fault"]["desc"]}:'
            f'{result["fault"]["symbol"]}+0x{result["fault"]["offset"]:x}'
        )
    lines = [
        f'FRONTIER_VERDICT={verdict}',
        f'FRONTIER_REASON={reason}',
        f'FRONTIER_FAULT={fault_text}',
        f'FRONTIER_HIGHEST_PROGRESS={result["highest_progress"] or "none"}',
        f'FRONTIER_PASS_MARKERS={",".join(pass_hits) if pass_hits else "none"}',
        f'QEMU_RC={result["qemu_rc"]}',
    ]
    args.text_out.write_text("\n".join(lines) + "\n")
    print("\n".join(lines))

    if verdict in ("FRONTIER_PASS", "MOVED_LATER"):
        return 0
    if verdict in ("SAME_FAULT", "REGRESSED"):
        return 1
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
