#!/usr/bin/env python3
"""Parser cold switch storage: compile-stage equivalence and sanitizer edge gate.

Compare the unchanged certified release, disposable cold-pointer release,
and disposable cold-pointer ASan/UBSan variant on parser scenarios, including
nested switches at the implementation limit and failures after allocation.
No production source code is modified by this harness. Every successful
case compares exact emitted assembly, not merely exit status.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

def nest(depth: int) -> str:
    # Emit switches with distinct source scopes, some layers carrying cases.
    code = "long deepest(long x) {\n"
    for i in range(depth):
        code += "switch(x) { case " + str(i) + ": {\n"
    code += "x += 1;\n"
    for i in range(depth):
        code += "} default: break; }\n"
    return code + "return x;\n}\n"

def cases() -> list[tuple[str,str,bool]]:
    basic = """long parse_switch(long x,long y) {
    switch(x) {
      case -1: return 1;
      case 0:
        switch(y) { case -1: return -2;
                    case 0x100000000L: return 7;
                    default: return 8; }
      case 0x100000000L: return 3;
      default: return x;
    }
}"""
    many_cases = "long many(long n) {\nswitch(n) {\n"
    for i in range(128):
        many_cases += f"case {i}: return {i};\n"
    many_cases += "default: return n;\n}\n}\n"
    interleaved = """long mixed(long x) {
        switch(x) {
          case 0: x += 1; break;
          case 1: x += 2; break;
          default: x -= 3;
        }
        switch(x) {
          case 4: return 11;
          default: return x;
        }
    }
    long plain(long x) { return x+1; }
    """
    # Intentionally rejected sources must also release allocated contexts:
    duplicate_case = "long bad(long n) { switch(n) { case 1: break; case 1: break; } return n; }"
    duplicate_default = "long bad(long n) { switch(n) { default: break; default: break; } return n; }"
    malformed_after_switch = "long bad(long n) { switch(n) { case 0: return n; default: return 0; } unknown_token }"
    return [
        ("simple", basic, True),
        ("repeated", interleaved, True),
        ("case_128", many_cases, True),
        ("depth_15", nest(15), True),
        ("depth_16", nest(16), True),
        ("depth_17_rejected", nest(17), False),
        ("duplicate_case_rejected", duplicate_case, False),
        ("duplicate_default_rejected", duplicate_default, False),
        ("post_switch_parse_failure", malformed_after_switch, False),
    ]

def sha(path:Path) -> str | None:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None

def compile_case(driver:Path,source:Path,output:Path,sanitized:bool)->dict:
    env=os.environ.copy()
    if sanitized:
        env.update(ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:halt_on_error=1",
                   UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1")
    p=subprocess.run([str(driver),"-S",str(source),"-o",str(output)],
                     capture_output=True,text=True,env=env,timeout=120)
    error=(p.stderr or "")[-3500:]
    if any(term in error for term in (
            "ERROR: AddressSanitizer", "runtime error:", "LeakSanitizer",
            "AddressSanitizer:DEADLYSIGNAL", "SUMMARY: UndefinedBehaviorSanitizer")):
        raise RuntimeError(f"SANITIZER_FAULT {source.name} {driver}: {error}")
    return dict(rc=p.returncode,asm_sha256=sha(output),stderr_tail=error[-450:])

def main()->None:
    a=argparse.ArgumentParser()
    a.add_argument("--baseline-driver",type=Path,required=True)
    a.add_argument("--candidate-driver",type=Path,required=True)
    a.add_argument("--sanitize-driver",type=Path,required=True)
    a.add_argument("--out",type=Path,required=True)
    v=a.parse_args()
    v.out.mkdir(parents=True,exist_ok=True)
    outcomes=[]
    for name,text,should_succeed in cases():
        source=v.out/(name+".i")
        source.write_text(text+"\n")
        stats={}
        for label,driver,sanitized in (
                ("baseline",v.baseline_driver,False),
                ("candidate",v.candidate_driver,False),
                ("sanitized",v.sanitize_driver,True)):
            stats[label]=compile_case(driver,source,v.out/(name+"."+label+".s"),sanitized)
            accepted=stats[label]["rc"]==0 and stats[label]["asm_sha256"] is not None
            if accepted!=should_succeed:
                raise SystemExit(f"MINIC_PARSER_COLD_SWITCH_SAFETY=FAIL "+
                                 json.dumps(dict(case=name,label=label,observed=stats[label])))
        if should_succeed:
            hashes={stats[label]["asm_sha256"] for label in stats}
            if len(hashes)!=1:
                raise SystemExit("MINIC_PARSER_COLD_SWITCH_SAFETY=FAIL output-mismatch "+
                                 json.dumps(dict(case=name,data=stats)))
        else:
            if any(stats[label]["asm_sha256"] is not None for label in stats):
                raise SystemExit("MINIC_PARSER_COLD_SWITCH_SAFETY=FAIL unexpected .s "+name)
        record=dict(case=name,expected="pass" if should_succeed else "reject",arms=stats)
        outcomes.append(record)
        print("MINIC_PARSER_COLD_SWITCH_EDGE=PASS case=%s expected=%s three_arms=yes"%(
            name,record["expected"]),flush=True)
    (v.out/"summary.json").write_text(json.dumps(outcomes,indent=2)+"\n")
    print("MINIC_PARSER_COLD_SWITCH_SAFETY=PASS cases=%d arms=3 byte_exact_success=yes"%len(outcomes))

if __name__=="__main__":
    main()
