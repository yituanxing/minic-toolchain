#!/bin/sh
set -eu
: "${MINIC_CC:?set MINIC_CC to the debug compiler}"

source_root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
source="$source_root/tests/compiler/c0/x86_msabi_no_sse.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"${HOST_CC:-cc}" -std=gnu11 -E -P "$source" -o "$work/normal.i"
"$MINIC_CC" --target=x86_64-linux -fPIC -fshort-wchar -mno-sse \
    -S "$work/normal.i" -o "$work/no-sse.s"
"$MINIC_CC" --target=x86_64-linux -fPIC -fshort-wchar \
    -S "$work/normal.i" -o "$work/default.s"

sed -n '/^minic_ms_no_sse_entry:$/,/^\.size minic_ms_no_sse_entry,/p' \
    "$work/no-sse.s" >"$work/no-sse-ms.s"
sed -n '/^minic_ms_no_sse_entry:$/,/^\.size minic_ms_no_sse_entry,/p' \
    "$work/default.s" >"$work/default-ms.s"
test -s "$work/no-sse-ms.s"
test -s "$work/default-ms.s"
if grep -Eq '(%xmm[0-9]+|movdqu|movaps|movups)' "$work/no-sse.s"; then
    echo 'MSABI_NO_SSE=FAIL forbidden XMM operation in -mno-sse' >&2
    exit 1
fi
grep -F 'movdqu %xmm15,' "$work/default-ms.s" >/dev/null
grep -F ', %xmm15' "$work/default-ms.s" >/dev/null

"${HOST_CC:-cc}" -no-pie -x assembler "$work/no-sse.s" \
    -o "$work/no-sse"
"${HOST_CC:-cc}" -no-pie -x assembler "$work/default.s" \
    -o "$work/default"
"$work/no-sse"
"$work/default"
"${HOST_CC:-cc}" -O0 -fPIC -fshort-wchar -mno-sse "$source" \
    -o "$work/gnu"
"$work/gnu"

"${HOST_CC:-cc}" -std=gnu11 -DMINIC_MSABI_FLOAT_NEGATIVE -E -P \
    "$source" -o "$work/float.i"
if "$MINIC_CC" --target=x86_64-linux -mno-sse -S "$work/float.i" \
        -o "$work/float.s" >"$work/float.out" 2>"$work/float.err"; then
    echo 'MSABI_NO_SSE=FAIL floating ABI accepted' >&2
    exit 1
fi
grep -F 'x86-64 -mno-sse rejects reachable floating' \
    "$work/float.err" >/dev/null

echo 'MSABI_NO_SSE=PASS msabi_sysv_call=1 zero_xmm=1 default_xmm15_saved=1 GNU_AS=2 runtime=3 floating_negative=1'
