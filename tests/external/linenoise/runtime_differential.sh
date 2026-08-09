#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/linenoise-runtime"}
assembly=${LINENOISE_ASSEMBLY:-"$root/build/linenoise-discovery/linenoise.s"}
vendor="$work/upstream"
archive="$work/linenoise.tar.gz"
minic_binary="$work/linenoise-minic"
gcc_binary="$work/linenoise-gcc"
upstream=a473823d74b93eab2ba83480df16ed37617493f2

rm -rf "$work"
mkdir -p "$vendor"

if test ! -f "$assembly"; then
    printf '%s\n' "FAIL external/linenoise-runtime: missing MiniC assembly $assembly" >&2
    exit 1
fi

curl -fsSL "https://github.com/antirez/linenoise/archive/$upstream.tar.gz" -o "$archive"
tar -xzf "$archive" --strip-components=1 -C "$vendor"
test "$(git hash-object "$vendor/linenoise.c")" = 63f23ddaf0e06dea4d2ac04efa084c3ca275ad8c
test "$(git hash-object "$vendor/linenoise.h")" = 735629b78ed2302d407fb3b6c8e56c6ac24bd6b7

cat >"$work/runtime.c" <<'EOF'
#include "linenoise.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    char *line;
    char saved[128];
    FILE *stream;
    size_t count;

    if (argc != 2) return 90;
    if (!linenoiseHistorySetMaxLen(4)) return 11;
    if (!linenoiseHistoryAdd("alpha")) return 12;
    if (!linenoiseHistoryAdd("beta")) return 13;
    if (linenoiseHistorySave(argv[1]) != 0) return 14;

    line = linenoise("unused> ");
    if (line == NULL) return 15;
    printf("line=%s\n", line);
    linenoiseFree(line);

    stream = fopen(argv[1], "r");
    if (stream == NULL) return 16;
    count = fread(saved, 1, sizeof(saved) - 1, stream);
    if (ferror(stream)) {
        fclose(stream);
        return 17;
    }
    saved[count] = '\0';
    fclose(stream);
    printf("history=%s", saved);
    return 0;
}
EOF

# First prove that MiniC's emitted assembly is accepted by the real target assembler
# and can link against the real RISC-V glibc. The same GCC-built harness is used for
# both variants so differences are attributable to the linenoise translation unit.
"$riscv_cc" -std=gnu11 -O2 -static -I"$vendor" \
    "$assembly" "$work/runtime.c" -o "$minic_binary"
"$riscv_cc" -std=gnu11 -O2 -static -I"$vendor" \
    "$vendor/linenoise.c" "$work/runtime.c" -o "$gcc_binary"

run_variant() {
    name=$1
    binary=$2
    history=$3

    set +e
    printf 'hello from pipe\n' | "$qemu" "$binary" "$history" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"
    status=$?
    set -e
    printf '%s\n' "$status" >"$work/$name.status"
}

run_variant minic "$minic_binary" "$work/minic.history"
run_variant gcc "$gcc_binary" "$work/gcc.history"

minic_status=$(cat "$work/minic.status")
gcc_status=$(cat "$work/gcc.status")
if test "$gcc_status" -ne 0; then
    printf '%s\n' "FAIL external/linenoise-runtime: GCC reference exit=$gcc_status" >&2
    cat "$work/gcc.stdout" >&2 || true
    cat "$work/gcc.stderr" >&2 || true
    exit 1
fi
if test "$minic_status" -ne "$gcc_status"; then
    printf '%s\n' \
        "FAIL external/linenoise-runtime: exit differs minic=$minic_status gcc=$gcc_status" >&2
    cat "$work/minic.stdout" >&2 || true
    cat "$work/minic.stderr" >&2 || true
    exit 1
fi
if ! cmp -s "$work/minic.stdout" "$work/gcc.stdout"; then
    printf '%s\n' 'FAIL external/linenoise-runtime: stdout differs from GCC reference' >&2
    diff -u "$work/gcc.stdout" "$work/minic.stdout" >&2 || true
    exit 1
fi
if ! cmp -s "$work/minic.stderr" "$work/gcc.stderr"; then
    printf '%s\n' 'FAIL external/linenoise-runtime: stderr differs from GCC reference' >&2
    diff -u "$work/gcc.stderr" "$work/minic.stderr" >&2 || true
    exit 1
fi
if ! cmp -s "$work/minic.history" "$work/gcc.history"; then
    printf '%s\n' 'FAIL external/linenoise-runtime: history file differs from GCC reference' >&2
    diff -u "$work/gcc.history" "$work/minic.history" >&2 || true
    exit 1
fi
if ! grep -Fx 'line=hello from pipe' "$work/minic.stdout" >/dev/null; then
    printf '%s\n' 'FAIL external/linenoise-runtime: non-TTY input path did not return expected line' >&2
    exit 1
fi

stdout_bytes=$(wc -c <"$work/minic.stdout" | tr -d ' ')
history_bytes=$(wc -c <"$work/minic.history" | tr -d ' ')
minic_size=$(wc -c <"$minic_binary" | tr -d ' ')
gcc_size=$(wc -c <"$gcc_binary" | tr -d ' ')
printf '%s\n' \
    "PASS external/linenoise-runtime non-tty=pipe history=save differential=gcc-byte-exact exit=$minic_status stdout=$stdout_bytes history_bytes=$history_bytes minic_binary=$minic_size gcc_binary=$gcc_size"
