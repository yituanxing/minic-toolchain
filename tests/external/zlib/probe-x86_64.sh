#!/bin/sh
# Pin zlib v1.3.1 to its annotated release-tag commit, use real host
# preprocessing, and compare MiniC native zlib to an independent GCC build.
set -eu
: "${MINIC_CC:?set MINIC_CC to the built native minic-cc}"
: "${HOST_CC:=cc}"
case "$(uname -m)" in x86_64|amd64) ;; *) echo "SKIP zlib x86-64: native runner required"; exit 0;; esac
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
commit=51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf
curl -fsSL "https://github.com/madler/zlib/archive/$commit.tar.gz" -o "$work/zlib.tar.gz"
mkdir -p "$work/upstream"
tar -xzf "$work/zlib.tar.gz" -C "$work/upstream" --strip-components=1
src="$work/upstream"
test "$(git hash-object "$src/zlib.h")" = 8d4b932eaf6a0fbb8133b3ab49ba5ef587059fa0
test "$(git hash-object "$src/deflate.c")" = 012ea8148e8d21cf515a9ff47d4af719d049b276
test "$(git hash-object "$src/zconf.h")" = 62adc8d8431f2f9149ae0b1583915e21a28dd8b5
test "$(git hash-object "$src/test/example.c")" = c3521dd5934f716bc7c33fe1c5fd4a361c985e76
# Use the real upstream configure step so its generated zconf.h detects
# unistd.h and large-file support. A raw checkout lacks the declaration
# for lseek; GCC may tolerate implicit functions but MiniC rejects them.
# Both compilers must consume the same generated configuration.
if ! (cd "$src" && CC="$HOST_CC" CFLAGS="-O0 -std=c11" ./configure) \
        >"$work/configure.log" 2>&1; then
    echo "FAIL zlib x86-64 upstream configure" >&2
    tail -n 35 "$work/configure.log" >&2
    exit 1
fi
grep -F "Checking for unistd.h... Yes." "$work/configure.log" >/dev/null || {
    echo "FAIL zlib x86-64: host unistd.h feature detection failed" >&2
    exit 1
}
echo "PASS zlib x86-64 upstream configure unistd.h=Yes"

sources='adler32.c compress.c crc32.c deflate.c gzclose.c gzlib.c gzread.c gzwrite.c infback.c inffast.c inflate.c inftrees.c trees.c uncompr.c zutil.c'
# Reference build first: the same untouched upstream C files, no cache and
# no project-supplied test substitutes.
set --
for source in $sources; do set -- "$@" "$src/$source"; done
"$HOST_CC" -O0 -std=c11 -I"$src" "$src/test/example.c" "$@" \
    -o "$work/zlib-gcc.bin" || { echo "FAIL zlib x86: GCC reference build" >&2; exit 1; }
"$work/zlib-gcc.bin" >"$work/gcc.out" 2>"$work/gcc.err" || {
    echo "FAIL zlib x86: GCC reference runtime" >&2
    tail -n 30 "$work/gcc.err" >&2
    exit 1
}
echo "PASS zlib x86-64 GCC reference commit=$commit test=upstream-example"

passed=0
failed=0
set --
for source in $sources; do
    base=${source%.c}
    "$HOST_CC" -E -P -std=c11 -I"$src" "$src/$source" -o "$work/$base.i" || {
        echo "ZLIB_X86_64_BLOCKER source=$source passed=$passed stage=preprocess" >&2
        exit 1
    }
    if ! CORE_FAST_TRACE="${ZLIB_FRONTEND_LOOKUP_TRACE:-0}" "$MINIC_CC" --target=x86_64-linux -S "$work/$base.i" -o "$work/$base.s" \
            >"$work/$base.stdout" 2>"$work/$base.stderr"; then
        line=$(sed -n "s/.*$base\\.i:\\([0-9][0-9]*\\):.*/\\1/p" "$work/$base.stderr" | head -n 1)
        if test -z "$line"; then line=1; fi
        echo "ZLIB_X86_64_BLOCKER source=$source passed=$passed stage=compiler line=$line" >&2
        start=$((line > 12 ? line - 12 : 1))
        end=$((line + 12))
        echo "zlib preprocessed TU context=$source:$start-$end" >&2
        nl -ba "$work/$base.i" | sed -n "${start},${end}p" >&2
        sed -n '1,20p' "$work/$base.stderr" >&2
        if test "${ZLIB_X86_64_SCAN_ALL:-0}" = 1; then
            failed=$((failed + 1))
            continue
        fi
        exit 1
    fi
    if ! "$HOST_CC" -c -x assembler "$work/$base.s" -o "$work/$base.o"; then
        echo "ZLIB_X86_64_BLOCKER source=$source passed=$passed stage=assembler" >&2
        if test "${ZLIB_X86_64_SCAN_ALL:-0}" = 1; then
            failed=$((failed + 1))
            continue
        fi
        exit 1
    fi
    set -- "$@" "$work/$base.o"
    passed=$((passed + 1))
    echo "PASS zlib x86-64 TU $source count=$passed"
done
if test "$failed" -ne 0; then
    echo "ZLIB_X86_64_FRONTIER compiled=$passed total=15 blocked=$failed" >&2
    exit 1
fi
"$HOST_CC" -O0 -std=c11 -I"$src" "$src/test/example.c" "$@" \
    -o "$work/zlib-minic.bin"
set +e
"$work/zlib-minic.bin" >"$work/minic.out" 2>"$work/minic.err"
status=$?
set -e
if test "$status" -ne 0 || ! cmp -s "$work/gcc.out" "$work/minic.out" ||
   ! cmp -s "$work/gcc.err" "$work/minic.err"; then
    echo "FAIL zlib x86-64 native upstream example GCC/MiniC differential status=$status" >&2
    diff -u "$work/gcc.out" "$work/minic.out" >&2 || true
    diff -u "$work/gcc.err" "$work/minic.err" >&2 || true
    exit 1
fi
echo "PASS zlib x86-64 complete TUs=$passed oracle=GCC byte-exact example exit=0"

# Stronger independent runtime evidence: use upstream's unmodified minigzip
# as both GCC and MiniC-library clients. Cross-decompress each produced
# stream, compare against the exact original, and compare gzip bytes.
"$HOST_CC" -O0 -std=c11 -I"$src" "$src/test/minigzip.c" "$@" \
    -o "$work/minigzip-minic.bin"
set --
for source in $sources; do set -- "$@" "$src/$source"; done
"$HOST_CC" -O0 -std=c11 -I"$src" "$src/test/minigzip.c" "$@" \
    -o "$work/minigzip-gcc.bin"
python3 - >"$work/payload.bin" <<'PY'
import sys
sys.stdout.buffer.write(bytes(range(256)) * 32)
sys.stdout.buffer.write((b'zlib-minic-native-cross-oracle:\x00\x01\x7f\xff' * 4096))
PY
"$work/minigzip-gcc.bin" -c <"$work/payload.bin" >"$work/gcc.gz"
"$work/minigzip-minic.bin" -c <"$work/payload.bin" >"$work/minic.gz"
if ! cmp -s "$work/gcc.gz" "$work/minic.gz"; then
    echo "FAIL zlib x86-64: gzip bytes differ from GCC upstream minigzip" >&2
    exit 1
fi
"$work/minigzip-minic.bin" -d -c <"$work/gcc.gz" >"$work/minic-cross-roundtrip.bin"
"$work/minigzip-gcc.bin" -d -c <"$work/minic.gz" >"$work/gcc-cross-roundtrip.bin"
if ! cmp -s "$work/payload.bin" "$work/minic-cross-roundtrip.bin" ||
   ! cmp -s "$work/payload.bin" "$work/gcc-cross-roundtrip.bin"; then
    echo "FAIL zlib x86-64: GCC/MiniC cross-decompression mismatch" >&2
    exit 1
fi
echo "PASS zlib x86-64 upstream minigzip GCC/MiniC byte-exact gzip and cross-roundtrip"

