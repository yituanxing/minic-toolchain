#!/bin/sh
set -eu

export LC_ALL=C
export LANG=C

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/linux-batch"}
minic=${MINIC:-"$root/build/linux-compiler/bin/minic"}
cross_compile=${CROSS_COMPILE:-riscv64-linux-gnu-}
version=6.6.143
archive=${LINUX_ARCHIVE_CACHE:-"$work/linux-$version.tar.xz"}
src="$work/linux-$version"
out="$work/kbuild"
limit=${LINUX_BATCH_LIMIT:-100}
offset=${LINUX_BATCH_OFFSET:-0}
minic_jobs=${LINUX_BATCH_JOBS:-2}
sha256=dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932
url="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz"

case "$limit:$offset:$minic_jobs" in
    *[!0-9:]*|:*|*::*|*:) printf '%s\n' 'LINUX_BATCH_ERROR limit/offset/jobs must be non-negative integers' >&2; exit 2 ;;
esac
if test "$minic_jobs" -eq 0; then
    printf '%s\n' 'LINUX_BATCH_ERROR LINUX_BATCH_JOBS must be greater than zero' >&2
    exit 2
fi
if test ! -x "$minic"; then
    printf '%s\n' "LINUX_BATCH_ERROR MiniC not executable: $minic" >&2
    exit 2
fi

rm -rf "$work"
mkdir -p "$work" "$(dirname -- "$archive")"

archive_valid=false
if test -s "$archive" && printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c - >/dev/null 2>&1; then
    archive_valid=true
    printf '%s\n' "LINUX_ARCHIVE_CACHE hit path=$archive"
fi
if test "$archive_valid" != true; then
    rm -f "$archive" "$archive.tmp"
    curl -fsSL "$url" -o "$archive.tmp"
    mv "$archive.tmp" "$archive"
    printf '%s\n' "LINUX_ARCHIVE_CACHE fill path=$archive"
fi
printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c -
tar -xJf "$archive" -C "$work"

make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" defconfig >"$work/defconfig.log" 2>&1
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" -j4 prepare scripts >"$work/prepare.log" 2>&1

set +e
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" -n -j1 V=1 >"$work/kbuild-plan.raw" 2>"$work/kbuild-plan.stderr"
plan_status=$?
set -e

python3 - "$work/kbuild-plan.raw" "$out" "$work/tu-manifest.txt" "$work/selected-tus.txt" "$offset" "$limit" "$cross_compile" <<'PY'
from pathlib import Path, PurePosixPath
import re
import sys

plan_path = Path(sys.argv[1])
out_root = Path(sys.argv[2]).resolve()
manifest_path = Path(sys.argv[3])
selected_path = Path(sys.argv[4])
offset = int(sys.argv[5])
limit = int(sys.argv[6])
cross_prefix = sys.argv[7]
object_pattern = re.compile(r"(?:^|\s)-o\s+([^\s;]+\.o)(?=\s|;|$)")
source_pattern = re.compile(r"(?:^|\s)([^\s;]+\.c)(?=\s|;|$)")
entries = []
seen = set()
for line in plan_path.read_text(errors="replace").splitlines():
    padded = f" {line} "
    if cross_prefix not in line or " -c " not in padded:
        continue
    sources = source_pattern.findall(line)
    if not sources:
        continue
    object_matches = object_pattern.findall(line)
    if not object_matches:
        continue
    raw = object_matches[-1].strip("'\"")
    obj = Path(raw)
    if obj.is_absolute():
        try:
            rel = obj.resolve().relative_to(out_root)
        except ValueError:
            continue
    else:
        rel = obj
    rel_text = PurePosixPath(rel.as_posix()).as_posix()
    if rel_text.startswith(("scripts/", "tools/")) or "/scripts/" in rel_text:
        continue
    if rel_text.startswith("./"):
        rel_text = rel_text[2:]
    if not rel_text.endswith(".o") or rel_text in seen:
        continue
    source = sources[-1].strip("'\"")
    seen.add(rel_text)
    entries.append((rel_text, f"{rel_text[:-2]}.i", source))
with manifest_path.open("w") as output:
    for index, (obj, preprocessed, source) in enumerate(entries):
        output.write(f"{index}\t{obj}\t{preprocessed}\t{source}\n")
window = entries[offset:] if limit == 0 else entries[offset:offset + limit]
with selected_path.open("w") as output:
    for index, (obj, preprocessed, source) in enumerate(window, start=offset):
        output.write(f"{index}\t{obj}\t{preprocessed}\t{source}\n")
print(f"LINUX_BATCH_PLAN total_c_tus={len(entries)} offset={offset} requested_limit={limit} selected={len(window)}")
if not window:
    raise SystemExit("LINUX_BATCH_ERROR no C translation units selected from Kbuild plan")
PY

if test "$plan_status" -ne 0; then
    printf '%s\n' "LINUX_BATCH_PLAN_NOTE dry-run status=$plan_status; continuing because a C TU manifest was recovered"
fi
set --
for object in $(cut -f2 "$work/selected-tus.txt"); do
    set -- "$@" "$object"
done
set +e
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" KCFLAGS=-save-temps=obj -k -j4 "$@" >"$work/preprocess.log" 2>&1
materialize_status=$?
set -e
printf '%s\n' "LINUX_BATCH_MATERIALIZE status=$materialize_status selected=$(wc -l < "$work/selected-tus.txt" | tr -d ' ')"

python3 - "$minic" "$out" "$work/selected-tus.txt" "$work" "$minic_jobs" <<'PY'
from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import Counter
from pathlib import Path
import json, re, subprocess, sys, time
minic = Path(sys.argv[1]); out_root = Path(sys.argv[2]); selected_path = Path(sys.argv[3]); work = Path(sys.argv[4]); jobs = int(sys.argv[5])
entries = []
for line in selected_path.read_text().splitlines():
    index, obj, preprocessed, source = line.split("\t", 3)
    entries.append((int(index), obj, preprocessed, source))
asm_root = work / "minic-out"; log_root = work / "minic-stderr"
asm_root.mkdir(parents=True, exist_ok=True); log_root.mkdir(parents=True, exist_ok=True)
diag_re = re.compile(r"^(.*?):([0-9]+):([0-9]+):\s*(?:error:\s*)?(.*)$")
def compile_one(entry):
    index, obj, rel_i, source = entry
    input_path = out_root / rel_i; asm_path = asm_root / f"{rel_i}.s"; stderr_path = log_root / f"{rel_i}.stderr"
    asm_path.parent.mkdir(parents=True, exist_ok=True); stderr_path.parent.mkdir(parents=True, exist_ok=True)
    if not input_path.is_file() or input_path.stat().st_size == 0:
        return {"index":index,"object":obj,"input":rel_i,"source":source,"status":"PREPROCESS_MISSING","returncode":None,"line":None,"column":None,"message":"Kbuild object materialization did not leave selected .i","seconds":0.0}
    started=time.monotonic(); proc=subprocess.run([str(minic),"-S",str(input_path),"-o",str(asm_path)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True,errors="replace"); elapsed=time.monotonic()-started; stderr_path.write_text(proc.stderr)
    if proc.returncode==0 and asm_path.is_file() and asm_path.stat().st_size>0:
        return {"index":index,"object":obj,"input":rel_i,"source":source,"status":"PASS","returncode":0,"line":None,"column":None,"message":"","seconds":elapsed}
    first=f"MiniC failed without diagnostic (returncode={proc.returncode})"; line_number=None; column_number=None
    for raw_line in proc.stderr.splitlines():
        text=raw_line.strip()
        if not text: continue
        match=diag_re.match(text)
        if match:
            line_number=int(match.group(2)); column_number=int(match.group(3)); first=match.group(4).strip() or text; break
        if first.startswith("MiniC failed without diagnostic"): first=text
    return {"index":index,"object":obj,"input":rel_i,"source":source,"status":"FAIL","returncode":proc.returncode,"line":line_number,"column":column_number,"message":first,"seconds":elapsed}
results=[]
with ThreadPoolExecutor(max_workers=jobs) as pool:
    futures={pool.submit(compile_one,e):e for e in entries}
    for future in as_completed(futures): results.append(future.result())
results.sort(key=lambda r:r["index"]); counts=Counter(r["status"] for r in results); groups=Counter(r["message"] for r in results if r["status"]=="FAIL")
with (work/"batch-results.tsv").open("w") as output:
    output.write("index\tstatus\tinput\tsource\tline\tcolumn\treturncode\tseconds\tmessage\n")
    for r in results:
        message=r["message"].replace("\t"," ").replace("\n"," "); line="" if r["line"] is None else str(r["line"]); col="" if r["column"] is None else str(r["column"]); rc="" if r["returncode"] is None else str(r["returncode"])
        output.write(f'{r["index"]}\t{r["status"]}\t{r["input"]}\t{r["source"]}\t{line}\t{col}\t{rc}\t{r["seconds"]:.3f}\t{message}\n')
(work/"batch-results.json").write_text(json.dumps(results,indent=2,sort_keys=True))
context=[]
for r in results:
    if r["status"]!="FAIL" or r["line"] is None: continue
    p=out_root/r["input"]
    try: lines=p.read_text(errors="replace").splitlines()
    except OSError: continue
    li=r["line"]-1; start=max(0,li-3); end=min(len(lines),li+4); context.append(f'=== index={r["index"]} input={r["input"]} line={r["line"]} message={r["message"]} ===')
    for cur in range(start,end): context.append(f'{">" if cur==li else " "}{cur+1:8d}: {lines[cur]}')
    context.append("")
(work/"failure-contexts.txt").write_text("\n".join(context))
summary=["LINUX_BATCH_SUMMARY",f"selected_c_tus={len(results)}",f"pass={counts['PASS']}",f"fail={counts['FAIL']}",f"preprocess_missing={counts['PREPROCESS_MISSING']}",f"minic_jobs={jobs}","","minic_failure_groups:"]
if groups:
    for message,count in groups.most_common(): summary.append(f"  count={count} message={message}")
else: summary.append("  none")
summary.extend(["","failures:"])
for r in results:
    if r["status"]=="PASS": continue
    line="unknown" if r["line"] is None else str(r["line"]); rc="none" if r["returncode"] is None else str(r["returncode"])
    summary.append(f'  index={r["index"]} status={r["status"]} input={r["input"]} line={line} rc={rc} message={r["message"]}')
if counts["FAIL"]==0 and counts["PREPROCESS_MISSING"]==0: summary.append("  none")
summary_text="\n".join(summary)+"\n"; (work/"batch-summary.txt").write_text(summary_text); print(summary_text,end="")
if counts["FAIL"] or counts["PREPROCESS_MISSING"]: raise SystemExit(1)
PY
