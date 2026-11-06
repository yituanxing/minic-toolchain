#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cc=${HOST_CC:-cc}
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$cc" -E -P -std=gnu11 \
    "$root/tests/compiler/c0/riscv_linux_compiletime_assert_guards.c" \
    -o "$work/original.i"
for arch in riscv32 riscv64; do
    "$minic" --target="$arch-linux" -S "$work/original.i" \
        -o "$work/$arch.s"
    # Actual Linux jump_label.c ATOMIC_INIT(0/1) compile-time assertions
    # and the native-word READ_ONCE guard must never escape into GNU LD.
    if grep -E 'call __compiletime_assert_(304|305|329|minmax_signedness|auto_type_qualifiers)([^A-Za-z0-9_]|$)' \
        "$work/$arch.s"; then
        echo "LINUX_COMPILETIME_ASSERT=FAIL target=$arch constant_guard_leaked" >&2
        exit 1
    fi
    # Do not make attribute(error) functions disappear indiscriminately:
    # conditions depending on runtime data or effectful compound literals
    # must retain their unresolved fail-closed calls.
    grep -Fq 'call __compiletime_assert_dynamic' "$work/$arch.s"
    grep -Fq 'call __compiletime_assert_effectful' "$work/$arch.s"
    grep -Fq 'call __compiletime_assert_minmax_dynamic' "$work/$arch.s"
    echo "LINUX_COMPILETIME_ASSERT=PASS target=$arch scalar_init=2 rwonce=2 minmax_signedness=2 auto_type_cv=3 fail_closed=3"
done
