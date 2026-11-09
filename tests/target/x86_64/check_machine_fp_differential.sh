#!/usr/bin/env bash
# End-to-end host GCC differential for the typed x86 Core FP selector.
# Run after check-x86-machine-core-select builds its executable; TMPDIR must
# be set to a writable per-workspace scratch directory (host /tmp may be full).
set -euo pipefail

if [[ $# -ne 1 || ! -x "$1" || -z "${TMPDIR:-}" || ! -d "$TMPDIR" ]]; then
    echo 'usage: TMPDIR=/writable/scratch bash check_machine_fp_differential.sh path/to/x86-machine-core-select' >&2
    exit 2
fi

machine_selector=$1
scratch_dir=$(mktemp -d "${TMPDIR%/}/minic-x86-fp.XXXXXXXX")
cleanup() {
    rm -f "$scratch_dir/machine_fp.o" "$scratch_dir/machine_fp_diff"
    rmdir "$scratch_dir"
}
trap cleanup EXIT

"$machine_selector" --emit-fp |
    gcc -x assembler -c -o "$scratch_dir/machine_fp.o" -
gcc -O2 -fno-fast-math -Wall -Wextra -Werror -std=c11 \
    tests/target/x86_64/machine_fp_differential.c \
    "$scratch_dir/machine_fp.o" -lm -o "$scratch_dir/machine_fp_diff"
"$scratch_dir/machine_fp_diff"
