#!/usr/bin/env bash
set -Eeuo pipefail

if [[ $# -ne 5 ]]; then
  echo "usage: $0 <linux-src> <out-dir> <mode:gcc|mini> <jobs> <work-dir>" >&2
  exit 2
fi

src=$(realpath "$1")
out=$(realpath -m "$2")
mode=$3
jobs=$4
work=$(realpath -m "$5")
root=$(git rev-parse --show-toplevel)

case "$mode" in
  gcc|mini) ;;
  *) echo "invalid mode: $mode" >&2; exit 2 ;;
esac

mkdir -p "$out" "$work" "$work/bin"
events="$work/tool-events.tsv"
trace="$work/minic-kbuild.trace"
: >"$events"
: >"$trace"

# Every timed wrapper appends one TSV row under flock so parallel Kbuild jobs do
# not corrupt the performance ledger. Durations are monotonic-enough wall-clock
# samples for workload comparison; summed durations intentionally represent
# aggregate tool work and can exceed Kbuild wall time under -jN.
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
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$PERF_TOOL" "$start" "$end" "$dur_ms" "$rc" "$source_file" "$output_file"
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

# GNU target utilities are timed in both lanes so final-link/archive pressure is
# visible even before MiniAR/MiniLD become the Linux Kbuild owners.
make_wrapper "$work/bin/riscv64-linux-gnu-ar-perf" gnu_ar /usr/bin/riscv64-linux-gnu-ar
make_wrapper "$work/bin/riscv64-linux-gnu-ld-perf" gnu_ld /usr/bin/riscv64-linux-gnu-ld
make_wrapper "$work/bin/riscv64-linux-gnu-objcopy-perf" gnu_objcopy /usr/bin/riscv64-linux-gnu-objcopy

# A GCC wrapper classifies preprocessing, compilation and assembler-driver work.
cat >"$work/bin/riscv64-linux-gnu-gcc-perf" <<'SH'
#!/usr/bin/env bash
set -Eeuo pipefail
: "${PERF_EVENTS:?}"
: "${PERF_REAL_GCC:=/usr/bin/riscv64-linux-gnu-gcc}"
label=gcc_other
source_file=
output_file=
args=("$@")
for ((i=0; i<${#args[@]}; ++i)); do
  arg=${args[i]}
  case "$arg" in
    -E) label=gcc_cpp ;;
    -c) [[ "$label" == gcc_other ]] && label=gcc_cc || true ;;
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
"$PERF_REAL_GCC" "$@"
rc=$?
set -e
end=$(date +%s%N)
dur_ms=$(( (end - start) / 1000000 ))
{
  flock 9
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$label" "$start" "$end" "$dur_ms" "$rc" "$source_file" "$output_file"
} 9>>"$PERF_EVENTS"
exit "$rc"
SH
chmod +x "$work/bin/riscv64-linux-gnu-gcc-perf"

cc="$work/bin/riscv64-linux-gnu-gcc-perf"
ar="$work/bin/riscv64-linux-gnu-ar-perf"
ld="$work/bin/riscv64-linux-gnu-ld-perf"
objcopy="$work/bin/riscv64-linux-gnu-objcopy-perf"

if [[ "$mode" == mini ]]; then
  : "${MINIC_BIN:?MINIC_BIN is required for mini mode}"
  : "${MINIAS_BIN:?MINIAS_BIN is required for mini mode}"

  # Time MiniC itself.
  make_wrapper "$work/bin/minic-perf" minic "$MINIC_BIN"

  # Put MiniAS below GCC's assembler boundary and time the exact assembler
  # process. Query-only assembler calls stay with GNU as.
  cat >"$work/bin/riscv64-linux-gnu-as" <<'SH'
#!/usr/bin/env bash
set -Eeuo pipefail
: "${PERF_EVENTS:?}"
: "${MINIAS_BIN:?}"
query=0
out=
input=
opts=()
args=("$@")
for ((i=0; i<${#args[@]}; ++i)); do
  arg=${args[i]}
  case "$arg" in
    --version|-v|--help) query=1 ;;
    -march=*|-mabi=*) opts+=("$arg") ;;
    -o)
      if (( i + 1 < ${#args[@]} )); then out=${args[i+1]}; ((++i)); fi
      ;;
    -*) ;;
    *) [[ -f "$arg" ]] && input=$arg ;;
  esac
done
if (( query )) || [[ -z "$input" || -z "$out" ]]; then
  exec /usr/bin/riscv64-linux-gnu-as "$@"
fi
start=$(date +%s%N)
set +e
"$MINIAS_BIN" "${opts[@]}" -o "$out" "$input"
rc=$?
set -e
end=$(date +%s%N)
dur_ms=$(( (end - start) / 1000000 ))
{
  flock 9
  printf 'minias\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$start" "$end" "$dur_ms" "$rc" "$input" "$out"
} 9>>"$PERF_EVENTS"
exit "$rc"
SH
  chmod +x "$work/bin/riscv64-linux-gnu-as"

  # The existing Linux bridge uses GCC only for preprocessing and for the two
  # explicitly bounded vDSO/four purgatory C exceptions. All ordinary C bodies
  # run through MiniC; all generated assembly runs through MiniAS.
  export MINIC="$work/bin/minic-perf"
  export REAL_CC="$cc -B$work/bin/"
  # stage2_kbuild_cc expects REAL_CC to be one executable path, so provide a
  # tiny argv-preserving launcher rather than embedding arguments in REAL_CC.
  cat >"$work/bin/real-cc-mini" <<SH
#!/usr/bin/env bash
exec $(printf '%q' "$cc") -B$(printf '%q' "$work/bin/") "\$@"
SH
  chmod +x "$work/bin/real-cc-mini"
  export REAL_CC="$work/bin/real-cc-mini"
  export MINIC_KBUILD_TRACE="$trace"
  export MINIC_KEEP_INTERMEDIATES=0
  export MINIC_KBUILD_GCC_VDSO=1
  export MINIC_KBUILD_GCC_PURGATORY=1
  cc="$root/tests/external/linux/stage2_kbuild_cc.sh"
fi

start_ns=$(date +%s%N)
set +e
/usr/bin/time -v -o "$work/make.time" \
  make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
    CC="$cc" AR="$ar" LD="$ld" OBJCOPY="$objcopy" -j"$jobs" Image \
    >"$work/kbuild.log" 2>&1
rc=$?
set -e
end_ns=$(date +%s%N)
wall_ms=$(( (end_ns - start_ns) / 1000000 ))

MODE="$mode" RC="$rc" WALL_MS="$wall_ms" EVENTS="$events" OUT="$out" WORK="$work" \
python3 - <<'PY'
import csv, json, math, os
from collections import defaultdict
from pathlib import Path

mode = os.environ['MODE']
rc = int(os.environ['RC'])
wall_ms = int(os.environ['WALL_MS'])
events = Path(os.environ['EVENTS'])
out = Path(os.environ['OUT'])
work = Path(os.environ['WORK'])
rows = []
if events.exists():
    with events.open(newline='') as f:
        for r in csv.reader(f, delimiter='\t'):
            if len(r) != 7:
                continue
            tool, start, end, dur, status, source, output = r
            rows.append({
                'tool': tool,
                'duration_ms': int(dur),
                'rc': int(status),
                'source': source,
                'output': output,
            })

def percentile(values, q):
    if not values:
        return None
    xs = sorted(values)
    idx = max(0, min(len(xs) - 1, math.ceil(q * len(xs)) - 1))
    return xs[idx]

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
        'p50_ms': percentile(ds, .50),
        'p95_ms': percentile(ds, .95),
        'p99_ms': percentile(ds, .99),
        'max_ms': max(ds) if ds else None,
    }

slow = sorted(rows, key=lambda x: x['duration_ms'], reverse=True)[:30]
image = out / 'arch/riscv/boot/Image'
summary = {
    'schema': 1,
    'mode': mode,
    'result': 'PASS' if rc == 0 and image.is_file() else 'FAIL',
    'make_rc': rc,
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
print('LINUX_BUILD_PERF_V0', json.dumps(summary, separators=(',', ':')))
PY

exit "$rc"
