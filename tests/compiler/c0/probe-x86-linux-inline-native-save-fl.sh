#!/usr/bin/env bash
# A GNU always-inline zero-arg wrapper may return one locally captured x86
# flags snapshot. Preserve narrow source-ASM ownership and fail closed for
# changes to the original local/return/template/clobber contract.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf '%s\n' "X86_LINUX_INLINE_SAVE_FL=FAIL $*" >&2; exit 1; }
fixture="$root/tests/target/x86_64_linux_save_flags_inline.c"

"$host_cc" -E -P -std=gnu11 -x c "$fixture" -o "$work/positive.i"
"$host_cc" -std=gnu11 -c "$fixture" -o "$work/gcc.o" \
    || fail 'GNU GCC rejected original contract'
"$minic" --target=x86_64-linux -S "$work/positive.i" -o "$work/positive.s" \
    >"$work/positive.stdout" 2>"$work/positive.stderr" \
    || { sed -n '1,30p' "$work/positive.stderr" >&2; fail 'qualified Core inline refused'; }
as --64 "$work/positive.s" -o "$work/positive.o" \
    || fail 'GNU assembler rejected emitted flags snapshot'
if nm -u "$work/positive.o" | grep -Eq 'native_save_fl|arch_local_save_flags'; then
    fail 'GNU extern inline emitted unresolved non-inline symbol'
fi
objdump -d "$work/positive.o" >"$work/positive.objdump"
grep -Eq '[[:space:]]pushf([[:space:]]|$)' "$work/positive.objdump" \
    || fail 'qualified flags snapshot missing pushf'
grep -Eq '[[:space:]]pop[[:space:]]' "$work/positive.objdump" \
    || fail 'qualified flags snapshot missing pop'

assert_rejected() {
    local label=$1
    "$host_cc" -E -P -std=gnu11 -x c "$work/$label.c" \
        -o "$work/$label.i" || fail "$label preprocessing failed"
    if "$minic" --target=x86_64-linux -S "$work/$label.i" \
            -o "$work/$label.s" >"$work/$label.stdout" 2>"$work/$label.stderr"; then
        fail "$label unexpectedly passed narrow source-ASM admission"
    fi
    test ! -e "$work/$label.s" \
        || fail "$label left partially emitted assembler after rejection"
    grep -Eq 'Core IR does not yet support|cannot select complete typed MachineIR' \
        "$work/$label.stderr" \
        || { sed -n '1,30p' "$work/$label.stderr" >&2; fail "$label failed before expected Core/target boundary"; }
}

sed 's/unsigned long flags;/unsigned long flags; unsigned long second;/' \
    "$fixture" >"$work/two_locals.c"
assert_rejected two_locals

sed 's/return flags;/return 42;/' "$fixture" >"$work/other_return.c"
assert_rejected other_return

sed 's/pushf ; pop %0/pushfq ; pop %0/' "$fixture" >"$work/non_template.c"
assert_rejected non_template

sed 's/"memory");/"cc");/' "$fixture" >"$work/wrong_clobber.c"
assert_rejected wrong_clobber

printf '%s\n' 'X86_LINUX_INLINE_SAVE_FL=PASS qualified_gnu_inline=1 GNU_as=1 source_rejection=4 unresolved_inline=0'
