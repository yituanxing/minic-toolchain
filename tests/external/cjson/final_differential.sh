#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
vendor="$root/tests/vendor/cjson/upstream"
work=${BUILD_DIR:-"$root/build/ci-external"}/tests/external/cjson
assembly="$work/cJSON.s"
upstream_test="$vendor/test.c"
minic_binary="$work/cjson-upstream-test-minic"
gcc_binary="$work/cjson-upstream-test-gcc"

if test ! -f "$assembly"; then
    printf '%s\n' "FAIL external/cjson-final: missing MiniC assembly $assembly" >&2
    exit 1
fi
if test ! -f "$upstream_test"; then
    printf '%s\n' "FAIL external/cjson-final: missing upstream test.c" >&2
    exit 1
fi

actual_test_blob=$(git hash-object "$upstream_test")
if test "$actual_test_blob" != 986fc6eb3e77fc29cca2980647c357bf1fe1b1fe; then
    printf '%s\n' "FAIL external/cjson-final: upstream test.c blob=$actual_test_blob" >&2
    exit 1
fi

"$riscv_cc" -std=c11 -static -I"$vendor" \
    "$assembly" "$upstream_test" -lm -o "$minic_binary"
"$riscv_cc" -std=c11 -static -I"$vendor" \
    "$vendor/cJSON.c" "$upstream_test" -lm -o "$gcc_binary"

set +e
"$qemu" "$minic_binary" >"$work/upstream-minic.stdout" 2>"$work/upstream-minic.stderr"
minic_status=$?
"$qemu" "$gcc_binary" >"$work/upstream-gcc.stdout" 2>"$work/upstream-gcc.stderr"
gcc_status=$?
set -e

if test "$minic_status" -ne 0 || test "$gcc_status" -ne 0; then
    printf '%s\n' \
        "FAIL external/cjson-final: upstream test exit minic=$minic_status gcc=$gcc_status" >&2
    exit 1
fi

if ! cmp -s "$work/upstream-minic.stdout" "$work/upstream-gcc.stdout"; then
    printf '%s\n' 'FAIL external/cjson-final: stdout differs from GCC reference' >&2
    diff -u "$work/upstream-gcc.stdout" "$work/upstream-minic.stdout" >&2 || true
    exit 1
fi
if ! cmp -s "$work/upstream-minic.stderr" "$work/upstream-gcc.stderr"; then
    printf '%s\n' 'FAIL external/cjson-final: stderr differs from GCC reference' >&2
    diff -u "$work/upstream-gcc.stderr" "$work/upstream-minic.stderr" >&2 || true
    exit 1
fi

stdout_bytes=$(wc -c <"$work/upstream-minic.stdout" | tr -d ' ')
minic_size=$(wc -c <"$minic_binary" | tr -d ' ')
gcc_size=$(wc -c <"$gcc_binary" | tr -d ' ')
printf '%s\n' \
    "PASS external/cjson-final upstream=test.c-v1.7.19 differential=gcc-byte-exact exit=0 stdout=$stdout_bytes minic_object=$minic_size gcc_object=$gcc_size"
