#!/bin/sh
# GNU Win64-ABI caller + MiniC real function definition, including an outgoing
# SysV call which clobbers all registers Microsoft x64 requires preserved.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
impl="$root/tests/compiler/c0/x86_msabi_definition_native_impl.c"
driver="$root/tests/compiler/c0/x86_msabi_definition_native_driver.c"
negative="$root/tests/compiler/c0/x86_msabi_definition_native_negative.c"

test -x "$minic" || { echo 'MS_X64_DEFINITION=FAIL compiler missing' >&2; exit 1; }
"$host_cc" -E -P -std=gnu11 "$impl" -o "$work/impl.i"
MINIC_CORE_IR=strict "$minic" --target=x86_64-linux -S "$work/impl.i" \
    -o "$work/impl.s"
"$host_cc" -std=gnu11 -O2 -Wall -Wextra -Werror -no-pie \
    "$impl" "$driver" -o "$work/gnu-oracle"
"$host_cc" -std=gnu11 -O2 -Wall -Wextra -Werror -no-pie \
    "$work/impl.s" "$driver" -o "$work/minic-native"
"$work/gnu-oracle" >"$work/gnu.out"
"$work/minic-native" >"$work/minic.out"
cmp -s "$work/gnu.out" "$work/minic.out" || {
    echo 'MS_X64_DEFINITION=FAIL GNU/MiniC runtime differential' >&2
    exit 1
}
grep -F 'MS_X64_DEFINITION=PASS incoming=6 mixed_xmm=1 shadow=1 nonvolatile=12' \
    "$work/minic.out" >/dev/null
# One-source positional ABI: 5th/6th args are above the 32B incoming
# caller-allocated shadow area, and all Win64 nonvolatile vector regs save.
grep -F 'movq %rcx, %rax' "$work/impl.s" >/dev/null
grep -F 'movq %rdx, %rax' "$work/impl.s" >/dev/null
grep -F 'movq 48(%rbp), %rax' "$work/impl.s" >/dev/null
grep -F 'movq 56(%rbp), %rax' "$work/impl.s" >/dev/null
grep -F 'movdqu %xmm15,' "$work/impl.s" >/dev/null
grep -F ', %xmm15' "$work/impl.s" >/dev/null

for case_name in VARIADIC AGGREGATE INT128 LONG_DOUBLE; do
    "$host_cc" -E -P -std=gnu11 "-DNEG_DEF_$case_name" "$negative" \
        -o "$work/$case_name.i"
    if "$minic" --target=x86_64-linux -S "$work/$case_name.i" \
        -o "$work/$case_name.s" >"$work/$case_name.out" \
        2>"$work/$case_name.err"; then
        echo "MS_X64_DEFINITION=FAIL unsupported body accepted: $case_name" >&2
        exit 1
    fi
    grep -F 'ms_abi function definition requires supported fixed scalar signature' \
        "$work/$case_name.err" >/dev/null || {
        sed -n '1,8p' "$work/$case_name.err" >&2
        echo "MS_X64_DEFINITION=FAIL wrong negative stage: $case_name" >&2
        exit 1
    }
    test ! -e "$work/$case_name.s" || {
        echo "MS_X64_DEFINITION=FAIL partial assembly: $case_name" >&2
        exit 1
    }
done
echo 'MS_X64_DEFINITION_NATIVE=PASS gcc_oracle=1 incoming=6 xmm=1 nonvolatile=12 negative=4'
