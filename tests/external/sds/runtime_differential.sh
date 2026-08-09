#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/sds-runtime"}
assembly=${SDS_TEST_ASSEMBLY:-"$root/build/sds-discovery/sds-test.s"}
vendor="$root/tests/vendor/sds"
minic_binary="$work/sds-tests-minic"
gcc_binary="$work/sds-tests-gcc"
upstream=5347739b1581fcba74fd5cab1fc21d2aef317d71

rm -rf "$work"
mkdir -p "$work"

if test ! -f "$assembly"; then
    printf '%s\n' "FAIL external/sds-runtime: missing MiniC test assembly $assembly" >&2
    exit 1
fi

test "$(git hash-object "$vendor/sds.c")" = 3a7eae72f7591b3669af73954c42088ebbeccc4f
test "$(git hash-object "$vendor/sds.h")" = adcc12c0a7646d2c88a796ad5591931017140999
test "$(git hash-object "$vendor/sdsalloc.h")" = f43023c48438961445f6064ae8d0cc25f2b42f21
test "$(git hash-object "$vendor/testhelp.h")" = 450334046af86a5e0f00126f9790e9a14e170f84

# The MiniC translation unit already contains SDS_TEST_MAIN. Assemble/link it against
# the real target libc. Build the unchanged upstream reference with the target GCC and
# its native headers, then run both under the same QEMU environment.
"$riscv_cc" -static "$assembly" -o "$minic_binary"
"$riscv_cc" -std=c99 -pedantic -O2 -Wall -DSDS_TEST_MAIN -static -I"$vendor" \
    "$vendor/sds.c" -o "$gcc_binary"

set +e
timeout 60s "$qemu" "$minic_binary" >"$work/minic.stdout" 2>"$work/minic.stderr"
minic_status=$?
timeout 60s "$qemu" "$gcc_binary" >"$work/gcc.stdout" 2>"$work/gcc.stderr"
gcc_status=$?
set -e

if test "$gcc_status" -ne 0; then
    printf '%s\n' "FAIL external/sds-runtime: GCC reference exit=$gcc_status" >&2
    tail -n 80 "$work/gcc.stdout" >&2 || true
    tail -n 80 "$work/gcc.stderr" >&2 || true
    exit 1
fi
if test "$minic_status" -ne "$gcc_status"; then
    printf '%s\n' \
        "FAIL external/sds-runtime: exit differs minic=$minic_status gcc=$gcc_status" >&2
    tail -n 120 "$work/minic.stdout" >&2 || true
    tail -n 80 "$work/minic.stderr" >&2 || true
    exit 1
fi
if ! cmp -s "$work/minic.stdout" "$work/gcc.stdout"; then
    printf '%s\n' 'FAIL external/sds-runtime: stdout differs from GCC reference' >&2
    diff -u "$work/gcc.stdout" "$work/minic.stdout" >&2 || true
    exit 1
fi
if ! cmp -s "$work/minic.stderr" "$work/gcc.stderr"; then
    printf '%s\n' 'FAIL external/sds-runtime: stderr differs from GCC reference' >&2
    diff -u "$work/gcc.stderr" "$work/minic.stderr" >&2 || true
    exit 1
fi
if ! grep -E '[[:space:]]0 failed$' "$work/minic.stdout" >/dev/null; then
    printf '%s\n' 'FAIL external/sds-runtime: upstream test summary did not report zero failures' >&2
    tail -n 80 "$work/minic.stdout" >&2 || true
    exit 1
fi

summary=$(tail -n 1 "$work/minic.stdout")
minic_size=$(wc -c <"$minic_binary" | tr -d ' ')
gcc_size=$(wc -c <"$gcc_binary" | tr -d ' ')
printf '%s\n' \
    "PASS external/sds-runtime differential=gcc-byte-exact exit=$minic_status minic_binary=$minic_size gcc_binary=$gcc_size summary=$summary"
