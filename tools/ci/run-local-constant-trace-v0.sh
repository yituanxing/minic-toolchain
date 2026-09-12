#!/usr/bin/env bash
set -Eeuo pipefail
root=$(git rev-parse --show-toplevel)
cd "$root"
work="$root/build/local-constant-trace-v0"
build="$work/toolchain"
mkdir -p "$work"

python3 tools/ci/apply-local-integer-constants-v0.py
python3 tools/ci/patch-local-read-lvalue-v0.py
python3 tools/ci/patch-local-constant-trace-v0.py
git diff --check
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$build" all >/dev/null

cat >"$work/probe.c" <<'C'
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
riscv64-linux-gnu-gcc -E -P -std=gnu11 -x c "$work/probe.c" -o "$work/probe.i"
MINIC_LOCAL_CONSTANT_TRACE=1 "$build/bin/minic" -S "$work/probe.i" -o "$work/probe.s" \
    2>"$work/local-constant.trace"
riscv64-linux-gnu-gcc -x assembler -c "$work/probe.s" -o "$work/probe.o"
riscv64-linux-gnu-nm -u "$work/probe.o" >"$work/probe.undefined"
cat "$work/local-constant.trace"
cat "$work/probe.undefined"
echo TRACE_DONE
