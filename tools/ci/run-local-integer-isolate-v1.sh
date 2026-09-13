#!/usr/bin/env bash
set -Eeuo pipefail
root=$(git rev-parse --show-toplevel)
cd "$root"
work="$root/build/local-integer-isolate-v1"
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

run_variant() {
    name=$1
    shift
    git restore --source=HEAD -- src/core/core_lower.c src/core/core_lower_internal.h
    python3 tools/ci/apply-local-integer-constants-v0.py
    for patch in "$@"; do
        python3 "tools/ci/$patch"
    done
    git diff --check
    build="$work/$name"
    make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$build" all >/dev/null
    set +e
    CORE_FAST_TRACE=1 "$build/bin/minic" -S "$work/probe.i" -o "$work/$name.s" 2>"$work/$name.trace"
    rc=$?
    set -e
    echo "LOCAL_INTEGER_ISOLATE variant=$name rc=$rc"
    if [ "$rc" -ne 0 ]; then
        tail -40 "$work/$name.trace" || true
    fi
}

run_variant main_only
run_variant main_lvalue patch-local-read-lvalue-v0.py
run_variant main_assign patch-local-assignment-source-constant-v0.py
run_variant all patch-local-read-lvalue-v0.py patch-local-assignment-source-constant-v0.py

git restore --source=HEAD -- src/core/core_lower.c src/core/core_lower_internal.h

echo 'MINIC_LOCAL_INTEGER_ISOLATE_V1=PASS'
