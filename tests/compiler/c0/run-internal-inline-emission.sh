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
# The shared specialization/reachability pipeline can replace a referenced
# static inline with an integer-specialized clone. Either path is valid, but
# it must have a real reachable callee, not merely drop the return value.
if grep -F '  call used_inline' "$work/output.s" >/dev/null; then
    grep -F 'used_inline:' "$work/output.s" >/dev/null
else
    grep -E '^[[:space:]]+call[[:space:]]+__minic_inline_spec_[0-9]+_[0-9]+$' "$work/output.s" >/dev/null
    grep -E '^__minic_inline_spec_[0-9]+_[0-9]+:' "$work/output.s" >/dev/null
fi
if grep -F 'unused_long_double:' "$work/output.s" >/dev/null; then
    echo 'unreferenced internal inline unexpectedly emitted' >&2
    exit 1
fi

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -c "$work/output.s" -o "$work/output.o"
fi

# Independent native execution checks that the selected helper actually
# computes 6+1 under the real x86-64 ABI after specialization.
minic_cc=$(dirname "$minic")/minic-cc
"$minic_cc" --target=x86_64-linux -S "$work/input.c" -o "$work/output.x86.s"
cat >"$work/driver.c" <<'SRC'
extern int probe(void);
int main(void) { return probe() == 7 ? 0 : 1; }
SRC
"${HOST_CC:-cc}" -no-pie "$work/output.x86.s" "$work/driver.c" -o "$work/native"
"$work/native"

printf '%s\n' 'PASS compiler/c0/internal-inline-emission unused-long-double=omitted referenced-inline=reachable specialization=native-verified'
