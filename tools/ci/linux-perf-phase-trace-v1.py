#!/usr/bin/env python3
"""Phase attribution using preexisting MINIC_BOOTSTRAP_TRACE, frozen .i.

Only approximates phase times from timestamped stderr; NOT a throughput A/B,
new compiler validation, or Linux QEMU certification.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import threading
import time

STAGES=("parse","normalize","core-set","core-validate","layout","codegen")
PAT=re.compile(r"MINIC_BOOTSTRAP_TRACE stage=([a-z-]+) state=([a-z-]+) functions=(\d+)")

def digest(p):
    h=hashlib.sha256()
    with p.open("rb") as f:
        for part in iter(lambda:f.read(1048576),b""): h.update(part)
    return h.hexdigest()

def attribute(events):
    begin,elapsed,funcs={},{},{}
    for when,line in events:
        match=PAT.search(line)
        if not match or match.group(1) not in STAGES: continue
        stage,action,count=match.groups()
        if action=="begin":
            if stage in begin: raise RuntimeError("duplicate begin "+stage)
            begin[stage]=when
            funcs[stage]=int(count)
        elif action=="end-ok":
            if stage not in begin or stage in elapsed:
                raise RuntimeError("unmatched end "+stage)
            elapsed[stage]=round((when-begin[stage])/1000000,3)
        elif action=="end-fail":
            raise RuntimeError("compiler trace failed stage "+stage)
    if set(elapsed)!=set(STAGES):
        raise RuntimeError("missing stage trace "+repr(set(STAGES)-set(elapsed)))
    return elapsed,funcs

def compute_interstage_gaps(events):
    """Measure work outside the original six stage spans, without patching MiniC.

    Timestamps come from the same stderr reader. These gaps are NOT attributed
    to a specific pass until the compiler source between markers is examined.
    """
    starts, ends = {}, {}
    for when, line in events:
        match = PAT.search(line)
        if not match or match.group(1) not in STAGES:
            continue
        stage, action, _ = match.groups()
        if action == "begin":
            starts[stage] = when
        elif action == "end-ok":
            ends[stage] = when
    if set(starts) != set(STAGES) or set(ends) != set(STAGES):
        raise RuntimeError("incomplete stage boundaries for interstage attribution")
    gaps = {}
    for a, b in zip(STAGES, STAGES[1:]):
        ns = starts[b] - ends[a]
        if ns < 0:
            raise RuntimeError("overlapping or reordered compiler stages: " + a + " / " + b)
        gaps[a + "-to-" + b] = round(ns / 1000000, 3)
    return gaps


SPEC_STAGES=("spec-integer","spec-symbolic","spec-boolean","spec-finalize")
SYMBOLIC_SUBSTAGES=("spec-symbolic-direct","spec-symbolic-transitive")
INTEGER_SUBSTAGES=("spec-integer-direct","spec-integer-transitive")
BOOLEAN_SUBSTAGES=("spec-boolean-direct","spec-boolean-transitive")
BOOLEAN_COUNTER_RE=re.compile(r"MINIC_BOOLEAN_SCAN_PROFILE\s+(.+)")
SYMBOLIC_COUNTER_RE=re.compile(r"MINIC_SYMBOLIC_SCAN_PROFILE\s+(.+)")
SYMBOLIC_COUNTER_FIELDS=(
    "direct_expression_visits", "direct_call_visits",
    "direct_variant_lookups", "direct_variant_probes", "transitive_callers",
    "transitive_expression_visits", "transitive_owner_skips",
    "transitive_call_visits", "transitive_pointer_argument_evals",
    "transitive_variant_lookups", "transitive_variant_probes",
    "owner_index_ready", "direct_clones", "transitive_clones")
BOOLEAN_COUNTER_FIELDS=("local_queries","local_arena_scan_potential",
                        "callers","range_callers","expression_visits",
                        "call_expression_visits","variant_lookups","variant_probes",
                        "direct_clones","transitive_clones")

def read_boolean_counts(events):
    found=[]
    for _when,line in events:
        match=BOOLEAN_COUNTER_RE.search(line)
        if not match: continue
        fields=dict((k,int(v)) for k,v in
                    (word.split("=",1) for word in match.group(1).split()))
        if set(fields)!=set(BOOLEAN_COUNTER_FIELDS):
            raise RuntimeError("unexpected Boolean scan counters: "+repr(sorted(fields)))
        found.append(fields)
    if len(found)>1: raise RuntimeError("duplicate Boolean scan profile rows")
    return found[0] if found else None


def read_symbolic_counts(events):
    found=[]
    for _when,line in events:
        match=SYMBOLIC_COUNTER_RE.search(line)
        if not match: continue
        fields=dict((k,int(v)) for k,v in
                    (word.split("=",1) for word in match.group(1).split()))
        if set(fields)!=set(SYMBOLIC_COUNTER_FIELDS):
            raise RuntimeError("unexpected Symbolic scan counters: "+repr(sorted(fields)))
        if (fields["direct_call_visits"]>fields["direct_expression_visits"] or
            fields["transitive_owner_skips"]>fields["transitive_expression_visits"] or
            fields["transitive_call_visits"]>fields["transitive_expression_visits"]-fields["transitive_owner_skips"] or
            fields["owner_index_ready"] not in (0,1)):
            raise RuntimeError("invalid Symbolic scan census relationships")
        found.append(fields)
    if len(found)>1: raise RuntimeError("duplicate Symbolic scan census rows")
    return found[0] if found else None



# Read counters already present in the certified production compiler.
# This is solely opt-in diagnostic parsing, not a production C modification.
INTEGER_OWNER_RE = re.compile(
    r"MINIC_INTEGER_OWNER_FILTER ready=([01]) candidates=(\d+) "
    r"skipped_other_owner=(\d+) fallback=([01])"
)
PARSER_LOOKUP_RE = re.compile(r"MINIC_PARSER_LOOKUPS\s+(.+)")
PARSER_LOOKUP_KINDS = ("local", "function", "global", "typedef", "enum",
                       "fixed", "builtin_mem")


def read_integer_owner_counts(events):
    found = []
    for _when, line in events:
        m = INTEGER_OWNER_RE.search(line)
        if not m:
            continue
        ready, candidates, skipped, fallback = map(int, m.groups())
        if ready + fallback != 1 or skipped > candidates:
            raise RuntimeError("invalid integer owner filter counters")
        found.append({"ready": ready, "candidates": candidates,
                      "skipped_other_owner": skipped, "fallback": fallback})
    if len(found) != 1:
        raise RuntimeError("expected exactly one integer owner report; got " + str(len(found)))
    return found[0]


def read_parser_lookup_counts(events):
    found = []
    for _when, line in events:
        m = PARSER_LOOKUP_RE.search(line)
        if not m:
            continue
        pairs = re.findall(r"(\w+)=(\d+)/(\d+)", m.group(1))
        counts = {name: {"calls": int(calls), "steps": int(steps)}
                  for name, calls, steps in pairs}
        entities = {name: int(value) for name, value in
                    re.findall(r"(\w+)=(\d+)(?!/)", m.group(1))
                    if name in ("functions", "globals", "aliases", "expressions")}
        if (set(counts) != set(PARSER_LOOKUP_KINDS) or len(pairs) != len(counts) or
                set(entities) != {"functions", "globals", "aliases", "expressions"}):
            raise RuntimeError("malformed canonical parser lookup census")
        found.append({"lookups": counts, "entities": entities})
    if len(found) != 1:
        raise RuntimeError("expected exactly one parser lookup report; got " + str(len(found)))
    return found[0]


def summarize_specialization_trace(events):
    """Read existing opt-in specialization begin/end markers without touching IR."""
    started, total, calls = {}, {}, {}
    for when, line in events:
        match = PAT.search(line)
        if not match or match.group(1) not in SPEC_STAGES+SYMBOLIC_SUBSTAGES+INTEGER_SUBSTAGES+BOOLEAN_SUBSTAGES:
            continue
        stage, action, _ = match.groups()
        if action == "begin":
            if stage in started:
                raise RuntimeError("overlapping specialization stage: "+stage)
            started[stage] = when
        elif action.startswith("end"):
            if stage not in started:
                raise RuntimeError("unmatched specialization end: "+stage)
            elapsed = when - started.pop(stage)
            if elapsed < 0:
                raise RuntimeError("negative specialization duration")
            total[stage] = total.get(stage,0) + elapsed
            calls[stage] = calls.get(stage,0) + 1
    if started:
        raise RuntimeError("unfinished specialization stages: "+repr(sorted(started)))
    return {k:{"ms":round(total[k]/1000000,3),"calls":calls[k]}
            for k in sorted(total)}


def sample(executable,source,output,limit,collect_current_lookup_metrics=False):
    # Generic stage A/B retains its historical environment: older baseline
    # compilers may not support the newer production-only telemetry.
    env=dict(os.environ, MINIC_BOOTSTRAP_TRACE="1", MINIC_BOOLEAN_SCAN_PROFILE="1",
             MINIC_SYMBOLIC_SCAN_PROFILE="1")
    if collect_current_lookup_metrics:
        env.update(MINIC_INTEGER_OWNER_PERF="1", MINIC_PARSER_PERF_TRACE="1")
    begun=time.perf_counter_ns()
    with subprocess.Popen([str(executable),"-S",str(source),"-o",str(output)],
                          env=env,stdout=subprocess.DEVNULL,
                          stderr=subprocess.PIPE,text=True,errors="replace",bufsize=1) as p:
        timer=threading.Timer(limit,p.kill)
        timer.start()
        try:
            events=[(time.perf_counter_ns(),line.rstrip("\n")) for line in p.stderr]
            rc=p.wait()
        finally:
            timer.cancel()
    if rc!=0 or not output.is_file() or output.stat().st_size==0:
        raise RuntimeError("trace compiler failed rc="+str(rc)+" stderr="+repr(events[-3:]))
    stages,functions=attribute(events)
    gaps=compute_interstage_gaps(events)
    spec=summarize_specialization_trace(events)
    bool_counts=read_boolean_counts(events)
    symbolic_counts=read_symbolic_counts(events)
    integer_owner=(read_integer_owner_counts(events)
                   if collect_current_lookup_metrics else None)
    parser_lookups=(read_parser_lookup_counts(events)
                    if collect_current_lookup_metrics else None)
    return {"integer_owner":integer_owner, "parser_lookups":parser_lookups,
            "boolean_counts":bool_counts, "symbolic_counts":symbolic_counts, "wall_ms":round((time.perf_counter_ns()-begun)/1000000,3),
            "stage_ms":stages,"interstage_gap_ms":gaps,
            "specialization_ms":spec,"function_counts":functions, "assembly_sha":digest(output)}

def main():
    cli=argparse.ArgumentParser()
    cli.add_argument("--self-test",action="store_true")
    cli.add_argument("--expect-specialization",action="store_true")
    cli.add_argument("--expect-current-lookup-telemetry",action="store_true")
    cli.add_argument("--expect-symbolic-loops",action="store_true")
    cli.add_argument("--expect-symbolic-census",action="store_true")
    cli.add_argument("--expect-integer-loops",action="store_true")
    cli.add_argument("--expect-boolean-loops",action="store_true")
    cli.add_argument("--input",type=Path)
    cli.add_argument("--baseline",type=Path)
    cli.add_argument("--candidate",type=Path)
    cli.add_argument("--out",type=Path)
    ns=cli.parse_args()
    if ns.self_test:
        events=[]
        for i,stage in enumerate(STAGES):
            events.extend([(i*3000000,"MINIC_BOOTSTRAP_TRACE stage="+stage+" state=begin functions=10"),
                          (i*3000000+2000000,"MINIC_BOOTSTRAP_TRACE stage="+stage+" state=end-ok functions=10")])
        e,f=attribute(events)
        assert len(e)==6 and all(x==2 for x in e.values())
        gaps=compute_interstage_gaps(events)
        assert len(gaps)==5 and all(x==1 for x in gaps.values())
        sample_spec=[(0,"MINIC_BOOTSTRAP_TRACE stage=spec-symbolic state=begin functions=3"),
                     (5000000,"MINIC_BOOTSTRAP_TRACE stage=spec-symbolic state=end functions=4")]
        assert summarize_specialization_trace(sample_spec)["spec-symbolic"] == {"ms":5,"calls":1}
        nested=[(0,"MINIC_BOOTSTRAP_TRACE stage=spec-symbolic-direct state=begin functions=3"),
                (700000,"MINIC_BOOTSTRAP_TRACE stage=spec-symbolic-direct state=end-success functions=3"),
                (700000,"MINIC_BOOTSTRAP_TRACE stage=spec-symbolic-transitive state=begin functions=3"),
                (2100000,"MINIC_BOOTSTRAP_TRACE stage=spec-symbolic-transitive state=end-success functions=3")]
        assert summarize_specialization_trace(nested)["spec-symbolic-transitive"]["ms"]==1.4
        integer_probe=[(0,"MINIC_BOOTSTRAP_TRACE stage=spec-integer-direct state=begin functions=5"),
                       (800000,"MINIC_BOOTSTRAP_TRACE stage=spec-integer-direct state=end-success functions=5"),
                       (800000,"MINIC_BOOTSTRAP_TRACE stage=spec-integer-transitive state=begin functions=5"),
                       (2000000,"MINIC_BOOTSTRAP_TRACE stage=spec-integer-transitive state=end-success functions=5")]
        assert summarize_specialization_trace(integer_probe)["spec-integer-transitive"]["ms"]==1.2
        integer_example=[(0,"MINIC_INTEGER_OWNER_FILTER ready=1 candidates=100 skipped_other_owner=98 fallback=0")]
        assert read_integer_owner_counts(integer_example)["skipped_other_owner"]==98
        parser_example=[(0,"MINIC_PARSER_LOOKUPS local=4/35 function=3/9 global=2/15 typedef=8/40 enum=1/3 fixed=2/5 builtin_mem=4/7 functions=100 globals=80 aliases=50 expressions=2000")]
        assert read_parser_lookup_counts(parser_example)["lookups"]["local"]=={"calls":4,"steps":35}
        example=[(0,"MINIC_BOOLEAN_SCAN_PROFILE local_queries=3 local_arena_scan_potential=200 callers=4 range_callers=2 expression_visits=1000 call_expression_visits=9 variant_lookups=3 variant_probes=14 direct_clones=1 transitive_clones=1")]
        assert read_boolean_counts(example)["expression_visits"]==1000
        symbolic_example=[(0,"MINIC_SYMBOLIC_SCAN_PROFILE direct_expression_visits=10 direct_call_visits=3 direct_variant_lookups=1 direct_variant_probes=5 transitive_callers=2 transitive_expression_visits=100 transitive_owner_skips=88 transitive_call_visits=7 transitive_pointer_argument_evals=4 transitive_variant_lookups=1 transitive_variant_probes=2 owner_index_ready=1 direct_clones=1 transitive_clones=1")]
        assert read_symbolic_counts(symbolic_example)["transitive_owner_skips"]==88
        print("MINIC_PHASE_TRACE_SELFTEST=PASS six_stages=1 five_interstage_gaps=1 spec_timer=1 boolean_count_parser=1 symbolic_count_parser=1")
        return
    if not all((ns.input,ns.baseline,ns.candidate,ns.out)):
        cli.error("all inputs required")
    for x in (ns.input,ns.baseline,ns.candidate):
        if not x.is_file(): raise RuntimeError("missing "+str(x))
    ns.out.mkdir(parents=True,exist_ok=True)
    results={}
    for mode,executable in (("before",ns.baseline),("after",ns.candidate)):
        results[mode]=sample(executable.resolve(),ns.input.resolve(),ns.out/(mode+".s"),
                             180,ns.expect_current_lookup_telemetry)
    if results["before"]["assembly_sha"]!=results["after"]["assembly_sha"]:
        raise RuntimeError("different assembly bytes; cannot attribute performance")
    if ns.expect_symbolic_census and not ns.expect_symbolic_loops:
        raise RuntimeError("symbolic census requires symbolic direct/transitive timers")
    if ns.expect_symbolic_loops and not ns.expect_specialization:
        raise RuntimeError("symbolic loops require outer specialization tracing")
    if ns.expect_integer_loops and not ns.expect_specialization:
        raise RuntimeError("integer loops require outer specialization tracing")
    if ns.expect_boolean_loops and not ns.expect_specialization:
        raise RuntimeError("boolean loops require outer specialization tracing")
    if ns.expect_specialization and not set(SPEC_STAGES).issubset(results["after"]["specialization_ms"]):
        raise RuntimeError("diagnostic compiler did not emit all four specialization markers")
    data={"schema":"minic-existing-stage-trace-v1",
          "input_sha256":digest(ns.input),
          "results":results,
          "meaning":"approximate diagnosis, not paired speedup"}
    (ns.out/"phases.json").write_text(json.dumps(data,indent=2)+"\n")
    print("MINIC_PHASE_TRACE=PASS fixed_input=true assembly_equal=true")
    for stage in STAGES:
        old=results["before"]["stage_ms"][stage]
        new=results["after"]["stage_ms"][stage]
        print(f"MINIC_PHASE_TRACE_STAGE name={stage} old_ms={old:.2f} new_ms={new:.2f} delta_ms={old-new:.2f}")
    for gap in (a+"-to-"+b for a,b in zip(STAGES, STAGES[1:])):
        before=results["before"]["interstage_gap_ms"][gap]
        after=results["after"]["interstage_gap_ms"][gap]
        print(f"MINIC_PHASE_TRACE_GAP name={gap} old_ms={before:.2f} new_ms={after:.2f} delta_ms={before-after:.2f}")
    for mode in ("before","after"):
        item=results[mode]
        measured=sum(item["stage_ms"].values())+sum(item["interstage_gap_ms"].values())
        external=item["wall_ms"]-measured
        if external < -1:
            raise RuntimeError("phase boundaries exceed full compiler invocation")
        print(f"MINIC_PHASE_TRACE_RESIDUAL mode={mode} wall_ms={item['wall_ms']:.2f} "
              f"unclassified_before_parse_or_after_codegen_ms={external:.2f}")

    if ns.expect_integer_loops:
        for mode in ("before","after"):
            spec=results[mode]["specialization_ms"]
            if not all(n in spec and spec[n]["calls"]==1 for n in INTEGER_SUBSTAGES):
                raise RuntimeError("missing direct/transitive integer trace "+mode)
            total=sum(spec[n]["ms"] for n in INTEGER_SUBSTAGES)
            outer=spec["spec-integer"]["ms"]
            if total>outer+5:
                raise RuntimeError("integer substage timings exceed outer "+mode)
            for name in INTEGER_SUBSTAGES:
                item=spec[name]
                print(f"MINIC_INTEGER_SUBPHASE mode={mode} name={name} ms={item['ms']:.2f} calls={item['calls']}")
            print(f"MINIC_INTEGER_SUBPHASE_GROUP=PASS mode={mode} measured_ms={total:.2f} outer_ms={outer:.2f}")
    if ns.expect_boolean_loops:
        spec=results["after"]["specialization_ms"]
        if not all(s in spec and spec[s]["calls"]==1 for s in BOOLEAN_SUBSTAGES):
            raise RuntimeError("Boolean scan direct/transitive timers missing")
        sub=sum(spec[s]["ms"] for s in BOOLEAN_SUBSTAGES)
        outer=spec["spec-boolean"]["ms"]
        if sub>outer+5:
            raise RuntimeError("Boolean inner times exceed outer total")
        counts=results["after"]["boolean_counts"]
        if counts is None:
            raise RuntimeError("Boolean scan profile counters missing")
        if counts["call_expression_visits"]>counts["expression_visits"]:
            raise RuntimeError("Boolean CALL visits exceed scanned expressions")
        if counts["range_callers"]>counts["callers"]:
            raise RuntimeError("Boolean range callers exceed all cloned callers")
        for name in BOOLEAN_SUBSTAGES:
            print("MINIC_BOOLEAN_SUBPHASE name=%s ms=%.3f calls=%d" %
                  (name,spec[name]["ms"],spec[name]["calls"]))
        print("MINIC_BOOLEAN_SCAN=PASS "+ " ".join(
              f"{field}={counts[field]}" for field in BOOLEAN_COUNTER_FIELDS))
    if ns.expect_symbolic_loops:
        spec=results["after"]["specialization_ms"]
        if not all(n in spec and spec[n]["calls"]==1 for n in SYMBOLIC_SUBSTAGES):
            raise RuntimeError("missing direct or transitive symbolic marker")
        inner=sum(spec[n]["ms"] for n in SYMBOLIC_SUBSTAGES)
        outer=spec["spec-symbolic"]["ms"]
        if inner>outer+5:
            raise RuntimeError("substage timings exceed outer symbolic phase")
        for name in SYMBOLIC_SUBSTAGES:
            print(f"MINIC_SYMBOLIC_SUBPHASE name={name} ms={spec[name]['ms']:.2f} calls={spec[name]['calls']}")
        print(f"MINIC_SYMBOLIC_SUBPHASE_GROUP=PASS inner_ms={inner:.2f} outer_ms={outer:.2f}")
    if ns.expect_symbolic_census:
        counts=results["after"]["symbolic_counts"]
        if counts is None: raise RuntimeError("missing opt-in Symbolic census")
        print("MINIC_SYMBOLIC_SCAN=PASS " + " ".join(
              f"{name}={counts[name]}" for name in SYMBOLIC_COUNTER_FIELDS))
    if ns.expect_specialization:
        for stage in SPEC_STAGES:
            trace=results["after"]["specialization_ms"][stage]
            print(f"MINIC_SPEC_PHASE name={stage} ms={trace['ms']:.2f} calls={trace['calls']}")
        gap=results["after"]["interstage_gap_ms"]["normalize-to-core-set"]
        # Nested direct/transitive timers overlap with spec-symbolic.
        # Account only the four disjoint outer passes in the gap total.
        tracked=sum(results["after"]["specialization_ms"][name]["ms"] for name in SPEC_STAGES)
        print(f"MINIC_SPEC_UNATTRIBUTED gap_ms={gap:.2f} specialization_sum_ms={tracked:.2f} "
              f"other_gap_ms={gap-tracked:.2f}")
        print("MINIC_SPEC_PHASE_GROUP=PASS four_substages=true")
    print("MINIC_PHASE_TRACE_SCOPE=DIAGNOSTIC_ONLY not_QEMU_p1")

if __name__=="__main__":
    main()
