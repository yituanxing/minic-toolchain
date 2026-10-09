#!/usr/bin/env python3
"""M0: prove ELF push routing was added without modifying any original test body."""
from pathlib import Path
import hashlib
import re

ROOT=Path(__file__).resolve().parents[2]
WORKFLOW=ROOT/".github/workflows/miniobjcopy-strip-regressions-v1.yml"
ORIGINAL_SHA="5776fe8d2bd51fb485596274f44e8c9af5da7a5c"
OLD_IF="    if: (github.event_name == 'workflow_dispatch' && (inputs.mode == 'regressions' || inputs.mode == 'all')) || (github.event_name == 'push' && contains(github.event.head_commit.message, '[miniobjcopy-strip-regressions]'))"
NEW_IF="    if: (github.event_name == 'workflow_dispatch' && (inputs.mode == 'regressions' || inputs.mode == 'all')) || (github.event_name == 'push' && (contains(github.event.head_commit.message, '[miniobjcopy-strip-regressions]') || needs.route.outputs.elf_changed == 'true'))"

def git_sha(text):
    b=text.encode()
    return hashlib.sha1(b"blob "+str(len(b)).encode()+b"\0"+b).hexdigest()

def main():
    y=WORKFLOW.read_text()
    for key in ("needs: route", "fetch-depth: 0", "outputs.elf_changed", "select_miniobjcopy_elf_route_v1.py"):
        if key not in y: raise AssertionError(f"missing ELF router contract: {key}")
    if y.count(NEW_IF)!=1: raise AssertionError("regression routing guard changed")
    if y.count("  route:\n")!=1: raise AssertionError("expected exactly one route job")
    route=re.search(r"(?ms)^  route:\n.*?(?=^  regressions:\n)",y)
    if route is None: raise AssertionError("route job missing or misplaced")
    old=y[:route.start()]+y[route.end():]
    old=old.replace("  regressions:\n    needs: route\n"+NEW_IF,
                    "  regressions:\n"+OLD_IF)
    if git_sha(old)!=ORIGINAL_SHA:
        raise AssertionError("historic canonical MiniObjcopy/Strip workflow or test bodies changed")
    print("M0_MINIOBJCOPY_ELF_ROUTING=PASS historic_git_blob=1 original_3_jobs=preserved")

if __name__=="__main__":
    main()
