#!/usr/bin/env bash
# Linux 6.6.143 IBT gen_endbr: preserve source-qualified early-clobber u32,
# target physical output capture, and fail-closed template/constraint checks.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
fixture="$root/tests/target/x86_64_linux_gen_endbr.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf '%s\n' "X86_LINUX_GEN_ENDBR=FAIL $*" >&2; exit 1; }

"$host_cc" -std=gnu11 -c "$fixture" -o "$work/gnu.o" \
    || fail 'GNU GCC rejects reference gen_endbr'
"$host_cc" -std=gnu11 -E -P -x c "$fixture" -o "$work/positive.i"
"$minic" --target=x86_64-linux -fcf-protection=branch \
    -S "$work/positive.i" -o "$work/positive.s" \
    >"$work/stdout" 2>"$work/stderr" \
    || { sed -n '1,15p' "$work/stderr" >&2; fail 'qualified target form rejected'; }
as --64 "$work/positive.s" -o "$work/positive.o" \
    || fail 'GNU assembler rejected gen_endbr'
objdump -d "$work/positive.o" >"$work/objdump"
grep -Eq 'mov[[:space:]]+\$0x5e1f00c,%eax' "$work/objdump" \
    || fail 'original complement immediate not materialized'
grep -Eq 'not[[:space:]]+%eax' "$work/objdump" \
    || fail 'original NOT not materialized'

reject() {
    local label=$1
    local target=${2:-x86_64-linux}
    "$host_cc" -std=gnu11 -E -P -x c "$work/$label.c" -o "$work/$label.i"
    if "$minic" --target="$target" -S "$work/$label.i" \
         -o "$work/$label.s" >"$work/$label.stdout" 2>"$work/$label.stderr"; then
        fail "$label admitted invalid GNU target form"
    fi
    test ! -e "$work/$label.s" || fail "$label left partial assembler"
}
sed 's/0xfa1e0ff3/0xfa1e0ff2/' "$fixture" >"$work/non_template.c"
reject non_template
sed 's/"=&r"(endbr)/"=r"(endbr)/' "$fixture" >"$work/non_early.c"
reject non_early
sed 's/"=&r"(endbr)/"=&r"(endbr) : : "cc"/' "$fixture" \
    >"$work/wrong_clobber.c"
reject wrong_clobber
cp "$fixture" "$work/wrong_target.c"
reject wrong_target aarch64-linux
printf '%s\n' 'X86_LINUX_GEN_ENDBR=PASS GNU_as=1 complement_not=1 negative_qualified=4'
