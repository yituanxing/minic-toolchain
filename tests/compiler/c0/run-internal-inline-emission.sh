#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/internal-inline-emission
rm -rf "$work"
mkdir -p "$work"

cat >"$work/input.c" <<'SRC'
static inline int unused_long_double(long double left, long double right)
{
    return left < right;
}

static inline int used_inline(int value)
{
    return value + 1;
}

int probe(void)
{
    return used_inline(6);
}
SRC

"$minic" -S "$work/input.c" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'probe:' "$work/output.s" >/dev/null
grep -F 'used_inline:' "$work/output.s" >/dev/null
grep -F '  call used_inline' "$work/output.s" >/dev/null
if grep -F 'unused_long_double:' "$work/output.s" >/dev/null; then
    echo 'unreferenced internal inline unexpectedly emitted' >&2
    exit 1
fi

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -c "$work/output.s" -o "$work/output.o"
fi

printf '%s\n' 'PASS compiler/c0/internal-inline-emission unused-long-double=omitted referenced-inline=emitted'
