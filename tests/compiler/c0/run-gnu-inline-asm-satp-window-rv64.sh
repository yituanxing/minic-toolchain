#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
rv_cc=${RV_CC:-riscv64-linux-gnu-gcc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-satp-window
source="$root/tests/compiler/c0/gnu_inline_asm_satp_window.c"
rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$source" -o "$work/probe.i"
"$minic" -S "$work/probe.i" -o "$work/minic.s"
"$rv_cc" -O2 -fno-pic -fno-pie -S "$source" -o "$work/gcc.s"

check_window() {
    asm=$1
    tag=$2
    python3 - "$asm" "$tag" <<'PY'
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
tag = sys.argv[2]
lines = path.read_text(errors="replace").splitlines()
start = None
end = None
for i, line in enumerate(lines):
    if start is None and re.search(r"\bcsrw\s+satp\s*,", line):
        start = i
        continue
    if start is not None and re.search(r"\bcsrrw\s+[^,]+\s*,\s*satp\s*,", line):
        end = i
        break
if start is None or end is None or end <= start:
    print(f"SATP_WINDOW_{tag}=MISSING")
    raise SystemExit(2)
window = lines[start:end + 1]
unsafe = [(start + j + 1, line) for j, line in enumerate(window[1:-1], 1)
          if re.search(r"\bsp\b", line)]
print(f"SATP_WINDOW_{tag}_BEGIN={start + 1}")
print(f"SATP_WINDOW_{tag}_END={end + 1}")
print(f"SATP_WINDOW_{tag}_STACK_TOUCHES={len(unsafe)}")
for lineno, line in unsafe[:24]:
    print(f"SATP_WINDOW_{tag}_STACK_TOUCH line={lineno} text={line.strip()}")
if unsafe:
    raise SystemExit(1)
PY
}

check_window "$work/gcc.s" GCC
check_window "$work/minic.s" MINIC
printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_satp_window_rv64 stack_free_between_csrw_csrrw=1'
