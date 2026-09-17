#!/usr/bin/env bash
set -Eeuo pipefail

if [[ $# -ne 4 ]]; then
  echo "usage: $0 <linux-src> <out-dir> <jobs> <work-dir>" >&2
  exit 2
fi

src=$(realpath "$1")
out=$(realpath -m "$2")
jobs=$3
work=$(realpath -m "$4")
root=$(git rev-parse --show-toplevel)
: "${MINIC_BIN:?MINIC_BIN is required}"

mkdir -p "$out" "$work" "$work/bin"
events="$work/tool-events.tsv"
trace="$work/minic-kbuild.trace"
: >"$events"
: >"$trace"

cat >"$work/bin/timed-tool" <<'SH'
#!/usr/bin/env bash
set -Eeuo pipefail
: "${PERF_EVENTS:?}"
: "${PERF_TOOL:?}"
: "${PERF_REAL_TOOL:?}"
start=$(date +%s%N)
set +e
"$PERF_REAL_TOOL" "$@"
rc=$?
set -e
end=$(date +%s%N)
source_file=
output_file=
args=("$@")
for ((i=0; i<${#args[@]}; ++i)); do
  arg=${args[i]}
  case "$arg" in
    -o)
      if (( i + 1 < ${#args[@]} )); then output_file=${args[i+1]}; ((++i)); fi
      ;;
    *.c|*.i|*.s|*.S) source_file=$arg ;;
  esac
done
dur_ms=$(( (end - start) / 1000000 ))
{
  flock 9
  printf '%s\t%s\t%s\t%s\t%s\n' "$PERF_TOOL" "$dur_ms" "$rc" "$source_file" "$output_file" >&9
} 9>>"$PERF_EVENTS"
exit "$rc"
SH
chmod +x "$work/bin/timed-tool"

make_wrapper() {
  local path=$1 tool=$2 real=$3
  cat >"$path" <<SH
#!/usr/bin/env bash
export PERF_EVENTS=$(printf '%q' "$events")
export PERF_TOOL=$(printf '%q' "$tool")
export PERF_REAL_TOOL=$(printf '%q' "$real")
exec $(printf '%q' "$work/bin/timed-tool") "\$@"
SH
  chmod +x "$path"
}

make_wrapper "$work/bin/minic-perf" minic "$MINIC_BIN"
make_wrapper "$work/bin/riscv64-linux-gnu-ar-perf" gnu_ar /usr/bin/riscv64-linux-gnu-ar
make_wrapper "$work/bin/riscv64-linux-gnu-ld-perf" gnu_ld /usr/bin/riscv64-linux-gnu-ld
make_wrapper "$work/bin/riscv64-linux-gnu-objcopy-perf" gnu_objcopy /usr/bin/riscv64-linux-gnu-objcopy

cat >"$work/bin/riscv64-linux-gnu-gcc-perf" <<'SH'
#!/usr/bin/env bash
set -Eeuo pipefail
: "${PERF_EVENTS:?}"
real=/usr/bin/riscv64-linux-gnu-gcc
label=gcc_other
source_file=
output_file=
args=("$@")
for ((i=0; i<${#args[@]}; ++i)); do
  arg=${args[i]}
  case "$arg" in
    -E) label=gcc_cpp ;;
    -x)
      if (( i + 1 < ${#args[@]} )) && [[ "${args[i+1]}" == assembler* ]]; then label=gcc_as_driver; fi
      ;;
    -o)
      if (( i + 1 < ${#args[@]} )); then output_file=${args[i+1]}; fi
      ;;
    *.c|*.i|*.s|*.S) source_file=$arg ;;
  esac
done
start=$(date +%s%N)
set +e
"$real" "$@"
rc=$?
set -e
end=$(date +%s%N)
dur_ms=$(( (end - start) / 1000000 ))
{
  flock 9
  printf '%s\t%s\t%s\t%s\t%s\n' "$label" "$dur_ms" "$rc" "$source_file" "$output_file" >&9
} 9>>"$PERF_EVENTS"
exit "$rc"
SH
chmod +x "$work/bin/riscv64-linux-gnu-gcc-perf"

wrapper="$root/tests/external/linux/stage2_kbuild_cc.sh"
cc="$work/bin/riscv64-linux-gnu-gcc-perf"
start_ns=$(date +%s%N)
set +e
PERF_EVENTS="$events" \
MINIC="$work/bin/minic-perf" \
REAL_CC="$cc" \
MINIC_KEEP_INTERMEDIATES=0 \
MINIC_KBUILD_TRACE="$trace" \
CORE_FAST_TRACE=1 \
/usr/bin/time -v -o "$work/make.time" \
  make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
    CC="$wrapper" \
    AR="$work/bin/riscv64-linux-gnu-ar-perf" \
    LD="$work/bin/riscv64-linux-gnu-ld-perf" \
    OBJCOPY="$work/bin/riscv64-linux-gnu-objcopy-perf" \
    -j"$jobs" Image >"$work/kbuild.log" 2>&1
rc=$?
set -e
end_ns=$(date +%s%N)
wall_ms=$(( (end_ns - start_ns) / 1000000 ))

RC="$rc" WALL_MS="$wall_ms" EVENTS="$events" OUT="$out" WORK="$work" JOBS="$jobs" python3 - <<'PY'
import csv, json, math, os
from collections import defaultdict
from pathlib import Path

rc = int(os.environ['RC'])
wall_ms = int(os.environ['WALL_MS'])
events = Path(os.environ['EVENTS'])
out = Path(os.environ['OUT'])
work = Path(os.environ['WORK'])
jobs = int(os.environ['JOBS'])
rows = []
if events.exists():
    with events.open(newline='') as f:
        for r in csv.reader(f, delimiter='\t'):
            if len(r) != 5:
                continue
            tool, dur, status, source, output = r
            rows.append({'tool': tool, 'duration_ms': int(dur), 'rc': int(status), 'source': source, 'output': output})

def pct(values, q):
    if not values:
        return None
    xs = sorted(values)
    i = max(0, min(len(xs)-1, math.ceil(q*len(xs))-1))
    return xs[i]

by_tool = defaultdict(list)
for row in rows:
    by_tool[row['tool']].append(row)
summary_tools = {}
for tool, items in sorted(by_tool.items()):
    ds = [x['duration_ms'] for x in items]
    summary_tools[tool] = {
        'count': len(items),
        'failed': sum(x['rc'] != 0 for x in items),
        'aggregate_ms': sum(ds),
        'p50_ms': pct(ds, .50),
        'p95_ms': pct(ds, .95),
        'p99_ms': pct(ds, .99),
        'max_ms': max(ds) if ds else None,
    }
slow = sorted(rows, key=lambda x: x['duration_ms'], reverse=True)[:30]
image = out / 'arch/riscv/boot/Image'
summary = {
    'schema': 1,
    'result': 'PASS' if rc == 0 and image.is_file() else 'FAIL',
    'make_rc': rc,
    'jobs': jobs,
    'kbuild_wall_ms': wall_ms,
    'kbuild_wall_s': round(wall_ms / 1000, 3),
    'image_bytes': image.stat().st_size if image.is_file() else 0,
    'tools': summary_tools,
    'slowest_events': slow,
}
(work / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
with (work / 'slowest.tsv').open('w') as f:
    f.write('tool\tduration_ms\trc\tsource\toutput\n')
    for row in slow:
        f.write(f"{row['tool']}\t{row['duration_ms']}\t{row['rc']}\t{row['source']}\t{row['output']}\n")
print('LINUX_RUNTIME_BUILD_PROFILE_V1', json.dumps(summary, separators=(',', ':')))
PY

exit "$rc"
