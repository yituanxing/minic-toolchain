#!/usr/bin/env bash
set -Eeuo pipefail
root=$(git rev-parse --show-toplevel)
cd "$root"
work="$root/build/return-equality-regression-v0"
base="$work/baseline"
patched="$work/patched"
mkdir -p "$work"

cat >"$work/probe.c" <<'C'
typedef unsigned short u16;
typedef _Bool bool;
struct S { u16 x; };

bool probe_bool(const struct S *p) {
    return p->x != (typeof(p->x))~0U;
}

int probe_int(const struct S *p) {
    return p->x != (typeof(p->x))~0U;
}
C
riscv64-linux-gnu-gcc -E -P -std=gnu11 -x c "$work/probe.c" -o "$work/probe.i"

make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$base" all >/dev/null
CORE_FAST_TRACE=1 "$base/bin/minic" -S "$work/probe.i" -o "$work/baseline.s" \
    2>"$work/baseline.trace"
echo 'RETURN_EQUALITY_BASELINE=PASS'

python3 tools/ci/apply-local-integer-constants-v0.py
python3 tools/ci/patch-local-read-lvalue-v0.py
python3 tools/ci/patch-local-assignment-source-constant-v0.py
python3 tools/ci/patch-return-equality-trace-v0.py
git diff --check
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$patched" all >/dev/null

set +e
MINIC_RETURN_EQUALITY_TRACE=1 CORE_FAST_TRACE=1 \
    "$patched/bin/minic" -S "$work/probe.i" -o "$work/patched.s" \
    2>"$work/patched.trace"
rc=$?
set -e
cat "$work/patched.trace"
echo "RETURN_EQUALITY_PATCHED_RC=$rc"
if [ "$rc" -eq 0 ]; then
    riscv64-linux-gnu-gcc -x assembler -c "$work/patched.s" -o "$work/patched.o"
    riscv64-linux-gnu-nm -u "$work/patched.o" >"$work/patched.undefined"
    echo 'RETURN_EQUALITY_PATCHED=PASS'
else
    echo 'RETURN_EQUALITY_PATCHED=REPRODUCED'
fi
# This is a diagnostic gate: require baseline success and preserve patched result.
exit 0
