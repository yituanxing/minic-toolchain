#!/usr/bin/env python3
"""Classify MiniC RV64I codegen vs source-owned GNU inline/file assembly.

The experimental producer is:
  MINIC_RV64I_ASM_AUDIT=1 minic -S -march=rv64i -mabi=lp64 ...

Only MiniC-created provenance markers bracket the *user assembly template*.
Compiler-generated operand loads, spills, frame code and libcalls remain
outside the regions and must be RV64I. This is a textual regression gate,
not proof that unknown GAS directives expand to legal final machine words.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys

# Mirror the qualified RV64I textual opcode family in target/riscv64/isa.c.
BASE_I = set("""
add addi addiw addw and andi auipc
beq bge bgeu blt bltu bne ebreak ecall fence jal jalr
lb lbu ld lh lhu lui lw lwu or ori sb sd sh sll slli slliw
sllw slt slti sltiu sltu sra srai sraiw sraw
srl srli srliw srlw sub subw sw xor xori
beqz bgez bgt bgtu bgtz ble bleu blez bltz bnez
call j jr la lla li mv neg negw nop not ret seqz
sext.b sext.h sext.w sgtz sltz snez tail zext.b zext.h zext.w
""".split())
M = set("mul mulh mulhu mulhsu mulw div divu divw divuw rem remu remw remuw".split())
ZICSR = set("csrrw csrrs csrrc csrrwi csrrsi csrrci csrr csrw csrs csrc csrwi csrsi csrci rdcycle rdtime rdinstret".split())
# A directive may contain executable machine words, define a GAS macro or
# change assembler state. Report it instead of claiming complete proof.
ESCAPE = set("""
.insn .byte .2byte .4byte .8byte .word .long .short .quad .fill
.macro .endm .rept .irp .irpc .include .incbin .option .attribute
.pushsection .section .popsection .subsection
""".split())
LABEL = re.compile(r"^(?:[\w.$]+|\d+):\s*")
MNEMONIC = re.compile(r"^([a-zA-Z_.][a-zA-Z_0-9.]*)\b")
MARKER = re.compile(r"^# MINIC_RV64I_ASM_(BEGIN|END)$")


def isa(op: str) -> str:
    op = op.lower()
    if op in BASE_I:
        return "I"
    if op in M:
        return "M"
    if op.startswith(("amo", "lr.", "sc.")):
        return "A"
    if op in ZICSR:
        return "Zicsr"
    if op == "fence.i":
        return "Zifencei"
    if op.startswith("c."):
        return "C"
    if op.startswith("f") or op in ("fld", "fsd", "flw", "fsw"):
        return "F/D"
    return "UNKNOWN"


def inspect(path: Path) -> dict:
    counts = {"compiler": Counter(), "inline": Counter()}
    findings: list[dict] = []
    in_asm = False
    origins = 0
    marker_errors = 0
    directives = 0
    with path.open(encoding="utf-8", errors="replace") as f:
        for lineno, raw in enumerate(f, 1):
            stripped = raw.strip()
            marker = MARKER.fullmatch(stripped)
            if marker:
                if marker.group(1) == "BEGIN":
                    if in_asm:
                        marker_errors += 1
                        findings.append(dict(line=lineno, kind="NESTED_MARKER"))
                    else:
                        in_asm = True
                        origins += 1
                else:
                    if not in_asm:
                        marker_errors += 1
                        findings.append(dict(line=lineno, kind="UNPAIRED_END"))
                    else:
                        in_asm = False
                continue
            if "MINIC_RV64I_ASM_BEGIN" in raw or "MINIC_RV64I_ASM_END" in raw:
                marker_errors += 1
                findings.append(dict(line=lineno, kind="MALFORMED_MARKER"))
                continue
            region = "inline" if in_asm else "compiler"
            # Strip comments, then any leading labels. GAS also supports
            # multiple statements separated by semicolons.
            source = raw.split("#", 1)[0].strip()
            while source:
                label = LABEL.match(source)
                if label is None:
                    break
                source = source[label.end():].strip()
            for statement in source.split(";"):
                statement = statement.strip()
                if not statement:
                    continue
                match = MNEMONIC.match(statement)
                if match is None:
                    findings.append(dict(line=lineno, origin=region, kind="UNPARSED",
                                         text=statement[:96]))
                    marker_errors += 1
                    continue
                op = match.group(1).lower()
                if op.startswith("."):
                    if op in ESCAPE:
                        directives += 1
                        findings.append(dict(line=lineno, origin=region,
                                             kind="UNVERIFIED_DIRECTIVE", opcode=op))
                    continue
                kind = isa(op)
                counts[region][kind] += 1
                if (region == "compiler" and kind != "I") or kind == "UNKNOWN":
                    findings.append(dict(line=lineno, origin=region,
                                         kind="NON_I_OR_UNKNOWN", opcode=op, isa=kind))
    if in_asm:
        marker_errors += 1
        findings.append(dict(line=lineno if 'lineno' in locals() else 0,
                             kind="UNCLOSED_ASM"))
    return dict(file=str(path), compiler=dict(counts["compiler"]),
                inline=dict(counts["inline"]), inline_regions=origins,
                marker_errors=marker_errors, unverified_directives=directives,
                compiler_non_i=sum(n for k, n in counts["compiler"].items() if k != "I"),
                findings=findings)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, required=True)
    p.add_argument("--json-out", type=Path, required=True)
    p.add_argument("--batch-results", type=Path, help="corpus_replay.py batch-results.json")
    p.add_argument("--expected-files", type=int)
    p.add_argument("--require-markers", action="store_true")
    p.add_argument("--fail-on-compiler-non-i", action="store_true")
    args = p.parse_args()
    paths = sorted(args.root.rglob("*.s"))
    if not paths:
        print("RV64I_ORIGIN_AUDIT=FAIL no MiniC .s outputs", file=sys.stderr)
        return 2
    rows = [inspect(path) for path in paths]
    totals = {"compiler": Counter(), "inline": Counter()}
    for row in rows:
        for origin in totals:
            totals[origin].update(row[origin])
    findings = [{**finding, "file": row["file"]}
                for row in rows for finding in row["findings"]]
    selected = None
    successful = None
    incomplete = False
    if args.batch_results:
        batch = json.loads(args.batch_results.read_text())
        selected = len(batch)
        successful = sum(row["status"] == "PASS" for row in batch)
        incomplete = len(paths) != successful
    count_non_i = sum(n for k, n in totals["compiler"].items() if k != "I")
    marker_errors = sum(row["marker_errors"] for row in rows)
    unverified = sum(row["unverified_directives"] for row in rows)
    summary = dict(files=len(paths), selected=selected, compile_pass=successful,
                   incomplete=incomplete, inline_regions=sum(row["inline_regions"] for row in rows),
                   compiler=dict(sorted(totals["compiler"].items())),
                   inline=dict(sorted(totals["inline"].items())),
                   compiler_non_i=count_non_i, marker_errors=marker_errors,
                   unverified_directives=unverified,
                   files_with_compiler_non_i=sum(row["compiler_non_i"] > 0 for row in rows),
                   findings=sorted(findings, key=lambda x: (x["file"], x["line"]))[:200])
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps({"summary": summary, "files": rows}, indent=2))
    passed = (marker_errors == 0
              and (args.expected_files is None or len(paths) == args.expected_files)
              and (not args.require_markers or summary["inline_regions"] > 0)
              and (not args.fail_on_compiler_non_i or count_non_i == 0))
    print("RV64I_ORIGIN_AUDIT={} files={} selected={} compile_pass={} compiler_I={} "
          "compiler_non_I={} asm_regions={} asm_M={} asm_A={} asm_Zicsr={} "
          "asm_Zifencei={} unknown={} asm_escapes={} marker_errors={} report={}".format(
              "PASS" if passed else "FAIL", len(paths), selected, successful,
              totals["compiler"]["I"], count_non_i, summary["inline_regions"],
              totals["inline"]["M"], totals["inline"]["A"], totals["inline"]["Zicsr"],
              totals["inline"]["Zifencei"],
              totals["inline"]["UNKNOWN"], unverified, marker_errors, args.json_out))
    for finding in summary["findings"][:25]:
        print("ORIGIN_FINDING=" + json.dumps(finding, ensure_ascii=False), file=sys.stderr)
    # Incomplete C compilation is a separate coverage failure, not a false
    # claim about the successfully generated .s files.
    if incomplete:
        print("RV64I_ORIGIN_COVERAGE=INCOMPLETE "
              f"compiled={successful} selected={selected}", file=sys.stderr)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
