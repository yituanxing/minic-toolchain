#!/usr/bin/env python3
"""Optional GNU cflow evidence probe on explicit C translation units.

A parser-generated source-call diagram is not a complete reachable call graph.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--repo", default=".")
    p.add_argument("--output", default="asam-out")
    p.add_argument("--file", default="src/target/x86_64/machine_core_select.c")
    args = p.parse_args()
    root = Path(args.repo).resolve()
    dest = (root / args.output).resolve()
    source = (root / args.file).resolve()
    if not (root in dest.parents or dest == root) \
            or not (root in source.parents and source.is_file() and source.suffix == ".c"):
        p.error("paths must remain in the repository and input must be C source")
    dest.mkdir(parents=True, exist_ok=True)
    status = {
        "schema": "asam-v1-cflow-probe-1", "file": args.file,
        "tool": "GNU cflow", "status": "UNAVAILABLE",
        "limitations": [
            "Lexical C source-call approximation; not a full compiler AST.",
            "No build-config awareness in no-preprocess mode.",
            "Unresolved function pointers, macros, conditional compilation and asm.",
            "Never use this output alone to prove that code is dead."
        ]
    }
    if shutil.which("cflow"):
        try:
            run = subprocess.run(
                ["cflow", "--no-preprocess", "--format=dot", "--depth=2",
                 str(source.relative_to(root))],
                cwd=root, capture_output=True, text=True, timeout=60,
                check=False
            )
            (dest / "cflow-raw.dot").write_text(run.stdout[:1000000], encoding="utf-8")
            (dest / "cflow-warnings.txt").write_text(run.stderr[:20000], encoding="utf-8")
            status.update({
                "status": "GRAPH_AVAILABLE_PARTIAL" if run.returncode == 0
                          else "PARSER_FAILED",
                "returncode": run.returncode,
                "reported_edges": run.stdout.count("->"),
                "output_truncated": len(run.stdout) > 1000000,
            })
        except subprocess.TimeoutExpired:
            status["status"] = "TIMEOUT"
    (dest / "cflow-status.json").write_text(
        json.dumps(status, indent=2) + "\n", encoding="utf-8")
    print("ASAM cflow:", status["status"],
          "edges:", status.get("reported_edges", 0))

if __name__ == "__main__":
    main()
