#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIC_CC:=${PWD}/build/release/bin/minic-cc}"

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$repo/build/release"}/tests/x86-linux-paravirt-patch-site
mkdir -p "$work"
source="$repo/tests/compiler/c0/x86_linux_paravirt_patch_site.c"

gcc -std=gnu11 -E -P "$source" -o "$work/input.i"
"$MINIC_CC" --target=x86_64-linux -fno-PIE -mno-sse -S \
    "$work/input.i" -o "$work/output.s"
gcc -fno-pie -no-pie -x assembler -c "$work/output.s" -o "$work/output.o"
objcopy --dump-section .parainstructions="$work/sites.bin" "$work/output.o"
python3 - "$work/sites.bin" <<'PY'
from pathlib import Path
import sys
sites = Path(sys.argv[1]).read_bytes()
assert len(sites) == 16 and sites[8] == 1 and sites[9] == 5, sites.hex()
print('X86_PARAVIRT_PATCH_ELF=PASS site_len=5 type=1', flush=True)
PY

gcc -fno-pie -no-pie -x assembler "$work/output.s" -o "$work/minic"
gcc -std=gnu11 -fno-pie -no-pie "$source" -o "$work/gnu"
set +e
"$work/minic"; minic_rc=$?
"$work/gnu"; gnu_rc=$?
set -e
if [ "$minic_rc" -ne 42 ] || [ "$gnu_rc" -ne 42 ]; then
    echo "X86_PARAVIRT_PATCH_RUNTIME=FAIL minic=$minic_rc gnu=$gnu_rc" >&2
    exit 1
fi
echo 'X86_PARAVIRT_PATCH_RUNTIME=PASS minic=42 gnu=42'
