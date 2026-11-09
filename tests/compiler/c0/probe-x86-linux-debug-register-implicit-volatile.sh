#!/usr/bin/env bash
# GNU write-only extended ASM is implicitly volatile. Linux 6.6.143 DR0-DR7
# source admission must retain all twelve exact privileged x86 machine forms.
# These are assembly-only probes; never execute debug-register instructions.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
fixture="$root/tests/target/x86_64_linux_debug_register_smoke.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf '%s\n' "X86_DEBUG_REGISTER_VOLATILE=FAIL $*" >&2; exit 1; }

"$host_cc" -std=gnu11 -c "$fixture" -o "$work/gcc.o" \
    || fail 'GNU GCC rejected reference twelve-form source'
"$host_cc" -std=gnu11 -E -P -x c "$fixture" -o "$work/positive.i"
"$minic" --target=x86_64-linux -S "$work/positive.i" \
    -o "$work/positive.s" >"$work/stdout" 2>"$work/stderr" \
    || { sed -n '1,15p' "$work/stderr" >&2; fail 'qualified GNU forms rejected'; }
as --64 "$work/positive.s" -o "$work/positive.o" \
    || fail 'GNU assembler rejected privileged instructions'
objdump -d "$work/positive.o" >"$work/objdump"
for reg in 0 1 2 3 6 7; do
    grep -Eq "mov[[:space:]]+%db${reg},%rax" "$work/objdump" \
        || fail "debug register ${reg} read missing"
    grep -Eq "mov[[:space:]]+%rax,%db${reg}" "$work/objdump" \
        || fail "debug register ${reg} write missing"
done

reject() {
    local label=$1
    local target=${2:-x86_64-linux}
    "$host_cc" -std=gnu11 -E -P -x c "$work/$label.c" -o "$work/$label.i"
    if "$minic" --target="$target" -S "$work/$label.i" \
            -o "$work/$label.s" >"$work/$label.stdout" 2>"$work/$label.stderr"; then
        fail "$label was admitted despite invalid target contract"
    fi
    test ! -e "$work/$label.s" || fail "$label emitted partial assembler"
}
sed 's/asm("mov %0, %%db0" :: "r"(v));/asm("mov %0, %%db4" :: "r"(v));/' \
    "$fixture" >"$work/invalid_dr.c"
reject invalid_dr
sed 's/asm("mov %0, %%db0" :: "r"(v));/asm("mov %0, %%db0" :: "r"(v) : "memory");/' \
    "$fixture" >"$work/unapproved_clobber.c"
reject unapproved_clobber
sed 's/asm("mov %%db0, %0" : "=r"(v));/asm volatile("mov %%db0, %0" : "=r"(v));/' \
    "$fixture" >"$work/invalid_read_volatile.c"
reject invalid_read_volatile
cp "$fixture" "$work/wrong_target.c"
reject wrong_target aarch64-linux

printf '%s\n' 'X86_DEBUG_REGISTER_VOLATILE=PASS GNU_as=1 forms=12 source_rejections=4'
