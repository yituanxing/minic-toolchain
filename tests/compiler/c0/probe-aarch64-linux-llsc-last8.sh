#!/usr/bin/env bash
# Cross-platform MiniC MachineIR smoke/negative tests.  The four original
# Linux .i owners are tested separately by the pinned seven-runner workflow.
set -Eeuo pipefail
minic=${1:?MiniC compiler path required}
root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
test -x "$minic"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -E -P -x c "$root/aarch64_linux_llsc_last8.c" -o "$work/positive.i"
"$minic" --target=aarch64-linux -S "$work/positive.i" -o "$work/positive.s"
grep -Fq 'stlxr w12, x9, [x10]' "$work/positive.s"
grep -Fq 'ldxp x9, x12, [x10]' "$work/positive.s"
grep -Fq 'stxp w13, x15, x16, [x10]' "$work/positive.s"
grep -Fq 'ccmp x12, x14, 0, eq' "$work/positive.s"
echo 'A64_LLSC_MIR=PASS atomic64_dec cmpxchg128'

# The Source forms are exact ISA contracts.  One opcode substitution or
# constraint relaxation must not be silently carried into MachineIR.
sed 's/stlxr %w1/stxr %w1/' "$root/aarch64_linux_llsc_last8.c" > "$work/bad-llsc.c"
sed 's/ccmp %\[rh\]/cmp %[rh]/' "$root/aarch64_linux_llsc_last8.c" > "$work/bad-cmpxchg.c"
sed 's/\[v\] "+Q"/[v] "=Q"/' "$root/aarch64_linux_llsc_last8.c" > "$work/bad-memory.c"
for kind in bad-llsc bad-cmpxchg bad-memory; do
    cc -E -P -x c "$work/$kind.c" -o "$work/$kind.i"
    if "$minic" --target=aarch64-linux -S "$work/$kind.i" \
            -o "$work/$kind.s" >/dev/null 2>&1; then
        echo "A64_LLSC_MIR_NEGATIVE=FAIL $kind unexpectedly admitted" >&2
        exit 1
    fi
done
echo 'A64_LLSC_MIR_NEGATIVE=PASS opcode constraint3/3'
