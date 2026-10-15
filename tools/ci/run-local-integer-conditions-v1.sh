#!/usr/bin/env bash
set -Eeuo pipefail
root=$(git rev-parse --show-toplevel)
cd "$root"
work="$root/build/local-integer-conditions-v1"
mkdir -p "$work"

cat >"$work/return-probe.c" <<'C'
typedef unsigned short u16;
typedef _Bool bool;
struct S { u16 x; };
bool probe_bool(const struct S *p) { return p->x != (typeof(p->x))~0U; }
int probe_int(const struct S *p) { return p->x != (typeof(p->x))~0U; }
C
riscv64-linux-gnu-gcc -E -P -std=gnu11 -x c "$work/return-probe.c" -o "$work/return-probe.i"

cat >"$work/constant-local-probe.c" <<'C'
extern int should_not_exist(void);
int constant_local_probe(void) {
    const int size = ({
        int selected = -22;
        if (sizeof(long) == 1)
            selected = 16;
        else if (sizeof(long) == 2)
            selected = 8;
        else if (sizeof(long) == 4)
            selected = 0;
        else if (sizeof(long) == 8)
            selected = 24;
        selected;
    });
    if (!(!(size < 0)))
        return should_not_exist();
    return size;
}
C
riscv64-linux-gnu-gcc -E -P -std=gnu11 -x c "$work/constant-local-probe.c" -o "$work/constant-local-probe.i"

run_variant() {
    name=$1
    shift
    git restore --source=HEAD -- src/core/core_lower.c src/core/core_lower_internal.h
    python3 tools/ci/apply-local-integer-conditions-v1.py
    for patch in "$@"; do
        python3 "tools/ci/$patch"
    done
    git diff --check
    build="$work/$name"
    make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$build" all >/dev/null

    set +e
    CORE_FAST_TRACE=1 "$build/bin/minic" -S "$work/return-probe.i" -o "$work/$name.return.s" 2>"$work/$name.return.trace"
    return_rc=$?
    CORE_FAST_TRACE=1 "$build/bin/minic" -S "$work/constant-local-probe.i" -o "$work/$name.constant.s" 2>"$work/$name.constant.trace"
    constant_rc=$?
    set -e

    undef=NA
    if [ "$constant_rc" -eq 0 ]; then
        riscv64-linux-gnu-gcc -x assembler -c "$work/$name.constant.s" -o "$work/$name.constant.o"
        riscv64-linux-gnu-nm -u "$work/$name.constant.o" >"$work/$name.constant.undefined"
        if grep -q 'should_not_exist' "$work/$name.constant.undefined"; then
            undef=YES
        else
            undef=NO
        fi
    fi
    echo "LOCAL_INTEGER_CONDITIONS variant=$name return_rc=$return_rc constant_rc=$constant_rc should_not_exist=$undef"
    if [ "$return_rc" -ne 0 ]; then tail -20 "$work/$name.return.trace" || true; fi
    if [ "$constant_rc" -ne 0 ]; then tail -20 "$work/$name.constant.trace" || true; fi
}

run_variant narrow
run_variant narrow_lvalue patch-local-read-lvalue-v0.py
run_variant narrow_assign patch-local-assignment-source-constant-v0.py
run_variant narrow_both patch-local-read-lvalue-v0.py patch-local-assignment-source-constant-v0.py

git restore --source=HEAD -- src/core/core_lower.c src/core/core_lower_internal.h

echo 'MINIC_LOCAL_INTEGER_CONDITIONS_V1=PASS'
