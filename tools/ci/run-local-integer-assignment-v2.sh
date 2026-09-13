#!/usr/bin/env bash
set -Eeuo pipefail
root=$(git rev-parse --show-toplevel)
cd "$root"
work="$root/build/local-integer-assignment-v2"
build="$work/toolchain"
mkdir -p "$work"

python3 tools/ci/apply-local-integer-assignment-v2.py
git diff --check
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$build" all >/dev/null

cat >"$work/probe.c" <<'C'
typedef unsigned short u16;
typedef _Bool bool;
struct S { u16 x; };
extern int should_not_exist(void);

bool probe_bool(const struct S *p) { return p->x != (typeof(p->x))~0U; }
int probe_int(const struct S *p) { return p->x != (typeof(p->x))~0U; }

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

int nonconstant_merge(int x) {
    int selected = 3;
    if (x)
        selected = 7;
    if (selected == 3)
        return 31;
    return 47;
}

int escaped_local(void) {
    int selected = 9;
    int *p = &selected;
    *p = 12;
    if (selected == 9)
        return 91;
    return selected;
}
C
riscv64-linux-gnu-gcc -E -P -std=gnu11 -x c "$work/probe.c" -o "$work/probe.i"
set +e
CORE_FAST_TRACE=1 "$build/bin/minic" -S "$work/probe.i" -o "$work/probe.s" 2>"$work/probe.trace"
rc=$?
set -e
if [ "$rc" -ne 0 ]; then
    tail -80 "$work/probe.trace" || true
    echo "MINIC_LOCAL_INTEGER_ASSIGNMENT_V2=FAIL compile_rc=$rc"
    exit 1
fi
riscv64-linux-gnu-gcc -x assembler -c "$work/probe.s" -o "$work/probe.o"
riscv64-linux-gnu-nm -u "$work/probe.o" >"$work/probe.undefined"
if grep -q 'should_not_exist' "$work/probe.undefined"; then
    cat "$work/probe.undefined"
    echo 'MINIC_LOCAL_INTEGER_ASSIGNMENT_V2=FAIL dead_reference'
    exit 1
fi
# Structural guards: normal member comparisons still lower and uncertain/escaped
# locals must retain runtime control/data flow rather than being folded away.
grep -q '^probe_bool:' "$work/probe.s"
grep -q '^probe_int:' "$work/probe.s"
grep -q '^nonconstant_merge:' "$work/probe.s"
grep -q '^escaped_local:' "$work/probe.s"
echo 'MINIC_LOCAL_INTEGER_ASSIGNMENT_V2=PASS dead_reference=NO return_member=PASS guards=PASS'
