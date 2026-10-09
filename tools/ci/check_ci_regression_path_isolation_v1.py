#!/usr/bin/env python3
"""M0: prove costly unit/regression workflows ignore Runtime-only trigger files.

All original job bodies, positive source paths, allowed branch scopes and manual
dispatch are reconstructed byte-for-byte and verified against original Git SHA.
"""
from pathlib import Path
import ast
import fnmatch
import hashlib
import os
import re

ROOT=Path(__file__).resolve().parents[2]
BRANCHES=("agent/linux-expanded-kbuild-v0","agent/linux-perf-boolean-domain-v1")
SOURCES={
 "minic-rv64-focused-regressions-v1.yml":("1655301d8d169f524fed757446d4367792270427","9fa0cf8e6f39b511afb157012ef82ee873a5bc3f"),
 "miniar-regressions-v1.yml":("930653a2f96a8b042ede339582c0a3ae053b70f1","979969bb5231cabf878050936bdee51c9e8c6398"),
 "minild-regressions-v1.yml":("cea2cc16a25d9163142c2ddbbc67e73e3f7120c8","63bd80cc92382437d7588abca29e01a94285cc50"),
}
SHARED_ELF_PATH="      - 'elf/**'\n"
NEGATIVE=(
 "      - '!tools/ci/runtime-*-trigger.txt'\n"
 "      - '!tools/ci/linux-runtime-rest-init-frontier-v0.json'\n"
 "      - '!tools/ci/select_runtime_owner_modes_v1.py'\n"
)
EXTRA_NEGATIVE=(
 "      - '!tools/ci/select_miniobjcopy_elf_route_v1.py'\n"
 "      - '!tools/ci/check_ci_*'\n"
)
PERF_RV64_BRANCH="      - \"agent/linux-perf-boolean-domain-v1\"\n"
SOURCE_PATHS=(
 "    paths:\n"
 "      - 'src/**'\n"
 "      - 'include/**'\n"
 "      - 'compiler/**'\n"
 "      - 'preprocessor/**'\n"
 "      - 'assembler/**'\n"
 "      - 'archiver/**'\n"
 "      - 'linker/**'\n"
 "      - 'tools/**'\n"
 "      - 'tests/**'\n"
 "      - 'scripts/**'\n"
 "      - 'Makefile'\n"
 "      - '.github/actions/**'\n"
)

def gitblob(data):
 b=data.encode()
 return hashlib.sha1(b"blob "+str(len(b)).encode()+b"\0"+b).hexdigest()

def paths(text):
 h=text.split("\njobs:\n",1)[0]
 m=re.search(r"(?m)^    paths:\n((?:      - [^\n]+\n)+)",h)
 if not m:
  raise AssertionError("positive/negative push paths absent")
 return [ast.literal_eval(s.strip()[2:].strip()) for s in m.group(1).splitlines()]

def accepted(patterns, changed):
 # GitHub paths evaluated in order: negative patterns subtract only matching paths.
 def match_path(path):
  matches=False
  for pat in patterns:
   neg=pat.startswith("!")
   if fnmatch.fnmatchcase(path,pat[1:] if neg else pat):
    matches=not neg
  return matches
 return any(match_path(path) for path in changed)

def main():
 branch=os.environ.get("GITHUB_REF_NAME",BRANCHES[0])
 if branch not in BRANCHES: raise AssertionError("unknown branch")
 for name,(rt,pf) in SOURCES.items():
  c=(ROOT/".github/workflows"/name).read_text()
  if c.count(NEGATIVE)!=1: raise AssertionError(f"three Runtime-only exclusions missing: {name}")
  if c.count(EXTRA_NEGATIVE)!=1: raise AssertionError(f"CI-only exclusions missing: {name}")
  if c.count(SHARED_ELF_PATH)!=1: raise AssertionError(f"shared ELF source path missing or repeated: {name}")
  patterns=paths(c)
  if patterns[-5:]!=["!tools/ci/runtime-*-trigger.txt","!tools/ci/linux-runtime-rest-init-frontier-v0.json","!tools/ci/select_runtime_owner_modes_v1.py","!tools/ci/select_miniobjcopy_elf_route_v1.py","!tools/ci/check_ci_*"]:
   raise AssertionError(f"negation path ordering changed: {name}")
  if "tools/**" not in patterns:raise AssertionError(f"normal tools source changes no longer covered: {name}")
  for forbidden in (
      "tools/ci/runtime-timekeeping-trigger.txt",
      "tools/ci/runtime-notifier-trigger.txt",
      "tools/ci/runtime-init-irq-trigger.txt",
      "tools/ci/linux-runtime-rest-init-frontier-v0.json",
      "tools/ci/select_runtime_owner_modes_v1.py",
      "tools/ci/check_ci_trigger_coverage_v1.py",
      "tools/ci/check_ci_regression_path_isolation_v1.py",
      "tools/ci/select_miniobjcopy_elf_route_v1.py",
  ):
   if accepted(patterns,[forbidden]):raise AssertionError(f"unrelated Runtime change runs {name}: {forbidden}")
  for included in (
      "elf/src/reader.c", "elf/src/relocatable_writer.c", "elf/src/rewrite.c",
      "src/frontend/parse.c", "archiver/miniar.c", "tools/ci/linux-runtime-build-minic-profile-v1.sh",
      "tests/compiler/c0/check.c", ".github/workflows/"+name,
  ):
   if not accepted(patterns,[included]):raise AssertionError(f"real source/test edit suppressed: {name}: {included}")
  if not accepted(patterns,["tools/ci/runtime-timekeeping-trigger.txt","src/frontend/parse.c"]):
   raise AssertionError(f"one excluded path wrongly masks real source change: {name}")
  if branch==BRANCHES[0]:
   previous=c.replace(EXTRA_NEGATIVE,"").replace(SHARED_ELF_PATH,"").replace(NEGATIVE,"")
  else:
   original_paths=SOURCE_PATHS+"      - '.github/workflows/"+name+"'\n"+NEGATIVE
   original_branch_text=c
   if name in SOURCES:
    if c.count(PERF_RV64_BRANCH)!=1:
     raise AssertionError("Performance compiler T1 automatic branch missing")
    original_branch_text=c.replace(PERF_RV64_BRANCH,"")
   previous_elf_free=original_branch_text.replace(EXTRA_NEGATIVE,"").replace(SHARED_ELF_PATH,"")
   if previous_elf_free.count(original_paths)!=1:
    raise AssertionError(f"old Performance branch path insertion shape differs: {name}")
   previous=previous_elf_free.replace(original_paths,"")
  expected=rt if branch==BRANCHES[0] else pf
  if gitblob(previous)!=expected:
   raise AssertionError(f"reconstructed historical job/branch/full YAML changed: {name}")
 print(f"M0_REGRESSION_PATH_ISOLATION=PASS branch={branch} workflows=3 original_job_bodies=3 excluded_runtime_and_maintenance_paths=5 mixed_source_change_kept=1")

if __name__=="__main__":
 main()
