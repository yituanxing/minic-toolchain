#!/usr/bin/env python3
"""M0 prove four Performance experiment oracles survived 4->1 YAML consolidation.

Original executable test bodies, env vars, timeouts, build commands, artifact
uploads and pinned source blob SHA must remain exact. Routing retains old push
paths and First500's opt-in tag. This is T0, not a performance certificate.
"""
from pathlib import Path
import ast, hashlib, os, re, sys

ROOT=Path(__file__).resolve().parents[2]
NAMES=("linux-core-object-interval-top5-ab-v1.yml",
       "linux-gnu-constant-p-ice-regression-v1.yml",
       "linux-optimized-first500-verify-v1.yml",
       "linux-parser-scope-first500-ab-v1.yml")
JOBS=("top5","ice","first500","parser")
OLD_JOBS=("top5","semantics","perf500","perf500")
BLOBS=("5366fbb35022c973f80a11c6a8d3d349cde25d65",
       "01090b5d83f9687bfc1cdc0616e753e650f4e515",
       "e3cee016ced9ea01f9c6e0690839f7bce141f999",
       "df4ae079496233b826c05aebb6867c9014215f4d")
NEW=ROOT/".github/workflows/linux-performance-experiments-v1.yml"
SELECTOR=ROOT/"tools/ci/select_perf_experiment_modes_v1.py"

def gitsha(blob):
    return hashlib.sha1(b"blob "+str(len(blob)).encode()+b"\0"+blob).hexdigest()

def paths(y):
    h=y.split("\njobs:\n",1)[0]
    m=re.search(r"(?m)^    paths:\n((?:      - [^\n]*\n)+)",h)
    if m is None:
        raise AssertionError("no path filter")
    return [ast.literal_eval(v) if v.startswith(("\'", '"')) else v
            for row in m.group(1).splitlines()
            for v in [row.strip()[2:].strip()]]

def job_body(y,key):
    text=y.split("\njobs:\n",1)[1]
    m=re.search(r"(?ms)^  "+re.escape(key)+r":\n(.*?)(?=^  [\w-]+:\n|\Z)",text)
    if m is None: raise AssertionError("job missing: "+key)
    return m.group(1)

def main():
    if os.environ.get("GITHUB_REF_NAME","agent/linux-perf-boolean-domain-v1") != "agent/linux-perf-boolean-domain-v1":
        raise AssertionError("Performance-only oracle must not run on Runtime")
    new=NEW.read_text()
    h=new.split("\njobs:\n",1)[0]
    if 'agent/linux-perf-boolean-domain-v1' not in h:
        raise AssertionError("Perf branch no longer selected")
    if "workflow_dispatch:" not in h:
        raise AssertionError("manual owner modes lost")
    if set(re.findall(r"(?m)^  ([\w-]+):\s*$",new.split("\njobs:\n",1)[1])) != set(("route",)+JOBS):
        raise AssertionError("expected 4 preserved jobs plus cheap route")
    allowed=set(paths(new))
    old_union=set()
    for name,job,prior_job,sha in zip(NAMES,JOBS,OLD_JOBS,BLOBS):
        if (ROOT/".github/workflows"/name).exists():
            raise AssertionError("old workflow remains active: "+name)
        raw=(ROOT/".github/workflows-disabled"/name).read_bytes()
        if gitsha(raw)!=sha:
            raise AssertionError("historical archived source mutated: "+name)
        original=raw.decode("utf-8")
        old_union.update(pattern for pattern in paths(original) if not pattern.startswith(".github/workflows/"))
        body=job_body(new,job)
        required="    needs: route\n    if: (github.event_name == 'workflow_dispatch' && (inputs.mode == 'all' || inputs.mode == '"+job+"')) || (github.event_name == 'push' && needs.route.outputs."+job+" == 'true'"
        if job=="first500":
            required+=" && contains(github.event.head_commit.message, '[perf-first500]')"
        required+=")\n"
        if not body.startswith(required):
            raise AssertionError("manual / push routing changed for "+job)
        after=body[len(required):].rstrip("\n")
        before=job_body(original,prior_job)
        if job=="first500":
            first="    if: github.event_name == 'workflow_dispatch' || (github.event_name == 'push' && contains(github.event.head_commit.message, '[perf-first500]'))\n"
            if not before.startswith(first):
                raise AssertionError("historical First500 opt-in changed")
            before=before[len(first):]
        if after != before.rstrip("\n"):
            raise AssertionError("original executable job body changed for "+job)
    expected=old_union|{".github/workflows/linux-performance-experiments-v1.yml",
                        "tools/ci/select_perf_experiment_modes_v1.py"}
    if allowed!=expected:
        raise AssertionError(f"combined trigger union differs missing={sorted(expected-allowed)} surplus={sorted(allowed-expected)}")
    scope=SELECTOR.read_text()
    for name in JOBS:
        if '"'+name+'": (' not in scope:
            raise AssertionError("missing selector domain: "+name)
    print("M0_PERF_EXPERIMENTS=PASS archived_source_sha=4 full_job_bodies=4 isolated_manual_modes=4 push_path_union=1 first500_tag=1")

if __name__=="__main__":
    main()
