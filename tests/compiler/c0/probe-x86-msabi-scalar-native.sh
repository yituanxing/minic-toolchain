#!/bin/sh
# GCC-generated Win64 callees exercise MiniC's real SysV->MS_X64 bridge.
# All source/target phases must refuse unsupported ABI forms explicitly.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
client="$root/tests/compiler/c0/x86_msabi_scalar_native_client.c"
oracle="$root/tests/compiler/c0/x86_msabi_scalar_native_gnu.c"
reject="$root/tests/compiler/c0/x86_msabi_scalar_negative.c"

test -x "$minic" || { echo 'MS_X64_SCALAR=FAIL compiler missing' >&2; exit 1; }
"$host_cc" -E -P -std=gnu11 "$client" -o "$work/client.i"
MINIC_CORE_IR=strict "$minic" --target=x86_64-linux -S "$work/client.i" \
    -o "$work/client.s"

"$host_cc" -std=gnu11 -O2 -Wall -Wextra -Werror -no-pie \
    "$client" "$oracle" -o "$work/gnu-oracle"
"$host_cc" -std=gnu11 -O2 -Wall -Wextra -Werror -no-pie \
    "$work/client.s" "$oracle" -o "$work/minic-msabi"
"$work/gnu-oracle" >"$work/gnu.out"
"$work/minic-msabi" >"$work/minic.out"
cmp -s "$work/gnu.out" "$work/minic.out" || {
    echo 'MS_X64_SCALAR=FAIL MiniC/GCC runtime differential' >&2
    exit 1
}
grep -F 'MS_X64_SCALAR=PASS direct=6 indirect=6' "$work/minic.out"

# Every ABI call, including zero args, reserves 32 bytes shadow/home space;
# the first stack argument is precisely 32(%rsp), not SysV's 0(%rsp).
grep -F 'subq $32, %rsp' "$work/client.s" >/dev/null
grep -F '32(%rsp)' "$work/client.s" >/dev/null
grep -F '48(%rsp)' "$work/client.s" >/dev/null
grep -F 'call *%r11' "$work/client.s" >/dev/null

for case_name in VARARGS AGGREGATE INT128 LONG_DOUBLE DEFINITION; do
    "$host_cc" -E -P -std=gnu11 "-DNEG_$case_name" "$reject" \
        -o "$work/$case_name.i"
    if "$minic" --target=x86_64-linux -S "$work/$case_name.i" \
        -o "$work/$case_name.s" >"$work/$case_name.out" \
        2>"$work/$case_name.err"; then
        echo "MS_X64_SCALAR=FAIL unsupported case admitted: $case_name" >&2
        exit 1
    fi
    test ! -e "$work/$case_name.s" || {
        echo "MS_X64_SCALAR=FAIL partial output: $case_name" >&2
        exit 1
    }
    grep -Eq 'ms_abi.*(fixed.scalar|incoming ABI)' "$work/$case_name.err" || {
        sed -n '1,8p' "$work/$case_name.err" >&2
        echo "MS_X64_SCALAR=FAIL wrong rejection stage: $case_name" >&2
        exit 1
    }
done

echo 'MS_X64_SCALAR_NATIVE=PASS gcc_oracle=1 direct=6 indirect=6 negative=5 stack_shadow=1'
