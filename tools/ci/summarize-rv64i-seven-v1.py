#!/usr/bin/env python3
"""Fail-closed seven-shard Linux 6.6.143 RV64I assembly-provenance receipt.

Do not conflate a clean subset of .s files with a successful 3352/3352
compilation. Read every frozen index and its source-origin scanner receipt.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import sys

SHARDS = (
    ("first500", 0, 500),
    ("new500", 500, 500),
    ("next500", 1000, 500),
    ("next500b", 1500, 500),
    ("next500c", 2000, 500),
    ("next500d", 2500, 500),
    ("final352", 3000, 352),
)


def read_json(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def summarise(evidence: Path) -> dict:
    scanned = Counter()
    inline = Counter()
    opcodes = {"compiler": Counter(), "inline": Counter()}
    failures = []
    problems = []
    shard_rows = []
    seen = set()
    for name, first, count in SHARDS:
        matches = sorted(evidence.glob(f"rv64i-origin-{name}-*/rv64i-origin-report.json"))
        if len(matches) != 1:
            problems.append(f"{name}: expected one report, found {len(matches)}")
            shard_rows.append(dict(shard=name, expected=count, receipt_count=len(matches)))
            continue
        path = matches[0]
        batch_path = path.parent / "batch-results.json"
        if not batch_path.is_file():
            problems.append(f"{name}: missing batch-results.json")
            shard_rows.append(dict(shard=name, expected=count, missing_batch=True))
            continue
        try:
            data = read_json(path)
            batch = read_json(batch_path)
            s = data["summary"]
            if not isinstance(batch, list):
                raise ValueError("batch results must be an array")
            indices = [int(row["index"]) for row in batch]
            expected_indices = list(range(first, first + count))
            if indices != expected_indices:
                problems.append(f"{name}: selected indices differ from frozen [{first},{first+count})")
            if seen.intersection(indices):
                problems.append(f"{name}: duplicate global TU indices")
            seen.update(indices)
            passed = sum(row["status"] == "PASS" for row in batch)
            compiled = int(s["compile_pass"])
            files = int(s["files"])
            selected = int(s["selected"])
            if len(batch) != count or selected != len(batch):
                problems.append(f"{name}: selected {selected}/{len(batch)} != {count}")
            if compiled != passed or files != passed:
                problems.append(f"{name}: .s files {files}, compile_pass {compiled}, statuses PASS {passed}")
            if int(s.get("marker_errors", 0)) != 0:
                problems.append(f"{name}: invalid inline-asm provenance markers")
            if int(s.get("compiler_non_i", 0)) != 0:
                problems.append(f"{name}: non-RV64I compiler-generated mnemonics")
            for kind, amount in s["compiler"].items():
                scanned[kind] += int(amount)
            for kind, amount in s["inline"].items():
                inline[kind] += int(amount)
            for owner in ("compiler", "inline"):
                for mnemonic, amount in s["opcode_counts"][owner].items():
                    opcodes[owner][mnemonic] += int(amount)
            for row in batch:
                if row["status"] != "PASS":
                    failures.append({
                        "index": row["index"],
                        "source": row.get("source"),
                        "status": row["status"],
                        "message": str(row.get("message", ""))[:240],
                    })
            row = dict(shard=name, first=first, selected=selected, compile_pass=passed,
                       asm_files=files, non_i=int(s["compiler_non_i"]),
                       inline_regions=int(s["inline_regions"]),
                       marker_errors=int(s["marker_errors"]),
                       unchecked_directives=int(s.get("unverified_directives", 0)))
            shard_rows.append(row)
            print("RV64I_SEVEN_SHARD " + json.dumps(row, sort_keys=True))
        except (KeyError, TypeError, ValueError, OSError) as exc:
            problems.append(f"{name}: unreadable receipt: {exc}")
            shard_rows.append(dict(shard=name, expected=count, error=str(exc)))
    missing = sorted(set(range(3352)) - seen)
    if missing:
        problems.append(f"missing {len(missing)} frozen global indices")
    actual = sum(int(row.get("selected", 0)) for row in shard_rows)
    passed = sum(int(row.get("compile_pass", 0)) for row in shard_rows)
    report = {
        "schema": "minic-linux-rv64i-seven-3352-provenance-v1",
        "selected": actual,
        "compile_pass": passed,
        "failed": len(failures),
        "missing_indices": missing,
        "compiler_instruction_classes": dict(sorted(scanned.items())),
        "inline_instruction_classes": dict(sorted(inline.items())),
        "compiler_top_opcodes": opcodes["compiler"].most_common(30),
        "inline_top_opcodes": opcodes["inline"].most_common(30),
        "shards": shard_rows,
        "failed_tus": failures,
        "error_groups": Counter(row["message"] for row in failures).most_common(25),
        "problems": problems,
    }
    report["pass"] = (not problems and not failures and actual == 3352 and
                      passed == 3352 and sum(scanned[k] for k in scanned if k != "I") == 0)
    return report


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--evidence", type=Path, required=True)
    p.add_argument("--json-out", type=Path, required=True)
    args = p.parse_args()
    report = summarise(args.evidence)
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("RV64I_SEVEN_SUMMARY " + json.dumps({
        "selected": report["selected"], "compiled": report["compile_pass"],
        "failed": report["failed"], "missing": len(report["missing_indices"]),
        "compiler_I": report["compiler_instruction_classes"].get("I", 0),
        "compiler_non_I": sum(n for k, n in report["compiler_instruction_classes"].items()
                              if k != "I"),
        "inline_M": report["inline_instruction_classes"].get("M", 0),
        "inline_A": report["inline_instruction_classes"].get("A", 0),
        "inline_Zicsr": report["inline_instruction_classes"].get("Zicsr", 0),
        "inline_Zifencei": report["inline_instruction_classes"].get("Zifencei", 0),
        "errors": len(report["problems"]),
    }, sort_keys=True))
    for item in report["failed_tus"][:45]:
        print("RV64I_SEVEN_FAILED_TU " + json.dumps(item, sort_keys=True))
    for error in report["problems"]:
        print("RV64I_SEVEN_RECEIPT_ERROR " + error, file=sys.stderr)
    print("RV64I_SEVEN_COMPLETE=" + ("PASS" if report["pass"] else "INCOMPLETE"),
          "receipt=" + str(args.json_out))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
