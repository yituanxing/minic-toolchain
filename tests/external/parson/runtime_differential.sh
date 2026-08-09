#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/parson-runtime"}
assembly=${PARSON_ASSEMBLY:-"$root/build/parson-discovery/parson.s"}
vendor="$work/upstream"
archive="$work/parson.tar.gz"
minic_binary="$work/parson-upstream-tests-minic"
gcc_binary="$work/parson-upstream-tests-gcc"
upstream=ba29f4eda9ea7703a9f6a9cf2b0532a2605723c3

rm -rf "$vendor"
mkdir -p "$vendor"

if test ! -f "$assembly"; then
    printf '%s\n' "FAIL external/parson-runtime: missing MiniC assembly $assembly" >&2
    exit 1
fi

curl -fsSL "https://github.com/kgabis/parson/archive/$upstream.tar.gz" -o "$archive"
tar -xzf "$archive" --strip-components=1 -C "$vendor"

test "$(git hash-object "$vendor/parson.c")" = 526aab437b418fa909517361cf39dc3dca47a8d6
test "$(git hash-object "$vendor/parson.h")" = 40be490bfd631970aad31c814de8ff5f83fe7c59
test "$(git hash-object "$vendor/tests.c")" = 3cf97b5d096ccedb7de50d358bd896d4a1ea3e6f

"$riscv_cc" -std=c89 -pedantic-errors -DTESTS_MAIN -static -I"$vendor" \
    "$assembly" "$vendor/tests.c" -lm -o "$minic_binary"
"$riscv_cc" -std=c89 -pedantic-errors -DTESTS_MAIN -static -I"$vendor" \
    "$vendor/parson.c" "$vendor/tests.c" -lm -o "$gcc_binary"

set +e
"$qemu" "$minic_binary" "$vendor/tests" \
    >"$work/upstream-minic.stdout" 2>"$work/upstream-minic.stderr"
minic_status=$?
"$qemu" "$gcc_binary" "$vendor/tests" \
    >"$work/upstream-gcc.stdout" 2>"$work/upstream-gcc.stderr"
gcc_status=$?
set -e

if test "$gcc_status" -ne 0; then
    printf '%s\n' "FAIL external/parson-runtime: GCC reference exit=$gcc_status" >&2
    tail -n 80 "$work/upstream-gcc.stderr" >&2 || true
    exit 1
fi
if ! grep -Fx 'Tests failed: 0' "$work/upstream-gcc.stdout" >/dev/null; then
    printf '%s\n' 'FAIL external/parson-runtime: GCC reference did not pass upstream suite' >&2
    tail -n 80 "$work/upstream-gcc.stdout" >&2 || true
    exit 1
fi
if test "$minic_status" -ne "$gcc_status"; then
    printf '%s\n' \
        "FAIL external/parson-runtime: exit differs minic=$minic_status gcc=$gcc_status" >&2
    tail -n 80 "$work/upstream-minic.stderr" >&2 || true
    exit 1
fi
if ! cmp -s "$work/upstream-minic.stdout" "$work/upstream-gcc.stdout"; then
    printf '%s\n' 'FAIL external/parson-runtime: stdout differs from GCC reference' >&2
    diff -u "$work/upstream-gcc.stdout" "$work/upstream-minic.stdout" >&2 || true
    exit 1
fi
if ! cmp -s "$work/upstream-minic.stderr" "$work/upstream-gcc.stderr"; then
    printf '%s\n' 'FAIL external/parson-runtime: stderr differs from GCC reference' >&2
    diff -u "$work/upstream-gcc.stderr" "$work/upstream-minic.stderr" >&2 || true
    exit 1
fi
if ! grep -Fx 'Tests failed: 0' "$work/upstream-minic.stdout" >/dev/null; then
    printf '%s\n' 'FAIL external/parson-runtime: MiniC build did not pass upstream suite' >&2
    exit 1
fi

passed=$(sed -n 's/^Tests passed: //p' "$work/upstream-minic.stdout" | tail -n 1)
stdout_bytes=$(wc -c <"$work/upstream-minic.stdout" | tr -d ' ')
minic_size=$(wc -c <"$minic_binary" | tr -d ' ')
gcc_size=$(wc -c <"$gcc_binary" | tr -d ' ')
printf '%s\n' \
    "PASS external/parson-runtime upstream=1.5.3 tests=$passed differential=gcc-byte-exact exit=$minic_status stdout=$stdout_bytes minic_binary=$minic_size gcc_binary=$gcc_size"
