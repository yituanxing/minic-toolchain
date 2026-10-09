#!/usr/bin/env python3
"""Export a complete per-job CI routing audit from active YAML and 45-entry ledger.

Report is descriptive: GitHub path matching and job expressions are preserved as
source text, not falsely evaluated. This is not T1/T2/T3/T4 test evidence.
"""
from pathlib import Path
import argparse
import csv
import re

ROOT = Path(__file__).resolve().parents[2]
LEDGER = ROOT / "docs/ci/active-workflow-inventory-2026-10-09.tsv"

def get_job_if(body):
    lines=body.splitlines()
    for idx,line in enumerate(lines):
        m=re.match(r"^    if:\s*(.*)$",line)
        if m is None:
            continue
        expression=m.group(1).strip()
        if expression in (">",">-","|","|-"):
            bits=[]
            for later in lines[idx+1:]:
                if not later.startswith("      "):
                    break
                bits.append(later.strip())
            return " ".join(bits)
        return expression
    return ""

def list_jobs(yaml):
    if "\njobs:\n" not in yaml:
        raise ValueError("missing jobs")
    src=yaml.split("\njobs:\n",1)[1]
    marks=list(re.finditer(r"(?m)^  ([A-Za-z][\w-]*):\s*$",src))
    for i,match in enumerate(marks):
        end=marks[i+1].start() if i+1<len(marks) else len(src)
        body=src[match.end():end]
        yield match.group(1),get_job_if(body),re.findall(r"(?m)^      - name:\s*(.*)$",body)

def raw_paths(h):
    m=re.search(r"(?m)^    paths:\n((?:      - [^\n]*\n)+)",h)
    if m:
        return ";".join(x.strip()[2:].strip().strip("'\"") for x in m.group(1).splitlines())
    m=re.search(r"(?m)^    paths:\s*\[([^\n]+)\]",h)
    if m:
        return m.group(1)
    return "<unrestricted-by-path>"

def kind(push,guard):
    if not push:
        return "NO_PUSH"
    if "needs.route.outputs." in guard:
        return "PUSH_ROUTER_CONDITIONAL"
    if "github.event_name == 'workflow_dispatch'" in guard and "github.event_name == 'push'" not in guard:
        return "MANUAL_CONDITIONAL"
    if "head_commit.message" in guard:
        return "PUSH_TAG_CONDITIONAL"
    return "PUSH_UNGUARDED" if not guard else "PUSH_CONDITIONAL"

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--output",required=True)
    args=ap.parse_args()
    import os
    branch=os.environ.get("GITHUB_REF_NAME","agent/linux-expanded-kbuild-v0")
    with LEDGER.open(newline="") as f:
        inventory=list(csv.DictReader(f,delimiter="\t"))
    if len(inventory)!=45 or len({r["path"] for r in inventory})!=26:
        raise AssertionError("maintained 45/26 inventory changed")
    owned=[r for r in inventory if r["branch"]==branch]
    expected=25 if branch=="agent/linux-expanded-kbuild-v0" else 20 if branch=="agent/linux-perf-boolean-domain-v1" else None
    if expected is None or len(owned)!=expected:
        raise AssertionError("unexpected branch or incomplete owned inventory")
    records=[]
    for entry in owned:
        rel=entry["path"]
        txt=(ROOT/rel).read_text()
        header=txt.split("\njobs:\n",1)[0]
        push="  push:" in header
        paths=raw_paths(header) if push else "<not-push-triggered>"
        expected_jobs=set(entry["job_ids"].split(";"))
        observed=set()
        for job,condition,steps in list_jobs(txt):
            observed.add(job)
            records.append((
                branch,rel,entry["contract_owner"],entry["tier"],job,
                entry["trigger_types"],entry["literal_branch_lines"],
                paths,condition or "<no-job-if>",kind(push,condition),
                ";".join(steps),entry["git_blob_sha"]
            ))
        if observed!=expected_jobs:
            raise AssertionError(f"ledger job drift {rel}: {observed}!={expected_jobs}")
    dest=Path(args.output)
    dest.parent.mkdir(parents=True,exist_ok=True)
    columns=("branch","workflow","owner","tier","job","event_types","declared_branches",
             "push_paths","raw_job_if","push_dispatch_class","named_steps","ledger_git_blob_sha")
    with dest.open("w",newline="") as f:
        wr=csv.writer(f,delimiter="\t",lineterminator="\n")
        wr.writerow(columns)
        wr.writerows(records)
    print(f"M0_CI_JOB_TRIGGER_MATRIX=PASS branch={branch} workflows={len(owned)} jobs={len(records)} file={dest}")
    print("M0_CI_JOB_TRIGGER_MATRIX_STATUS=SOURCE_ONLY not_an_execution_certificate")

if __name__ == "__main__":
    main()
