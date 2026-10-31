#!/usr/bin/env bash
# LP64D whole machine-word record ingress/return must use exact-width native
# frame transport; GNU packed/partial tails still require byte transport.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/loongarch64_record_chunk_native_word.c" \
    -o "$work/word.i"
"$minic" --target=loongarch64-linux -S "$work/word.i" -o "$work/word.s"

body() {
    awk -v symbol="$1" \
        '$0 == symbol ":" { inside=1; next }
         inside && /^\.size / { exit }
         inside { print }' "$work/word.s"
}
for symbol in loong_word_echo loong_pair_echo; do
    body "$symbol" | grep -Eq '^  st\.d \$t0, \$sp, [08]$'
    body "$symbol" | grep -Eq '^  ld\.d \$a[01], \$sp, [08]$'
    if body "$symbol" | grep -Eq '^  (st\.b|ld\.bu) '; then
        echo "LOONGARCH64_RECORD_CHUNK=FAIL unexpected_byte_transport=$symbol" >&2
        exit 1
    fi
done
body loong_tail_echo | grep -Eq '^  st\.b \$t0, \$t2, 6$'
body loong_tail_echo | grep -Eq '^  ld\.bu \$t0, \$sp, 6$'
if body loong_tail_echo | grep -Eq '^  (st\.d|ld\.d) .*\$sp,'; then
    echo 'LOONGARCH64_RECORD_CHUNK=FAIL overread_or_overwrite_partial_tail' >&2
    exit 1
fi
echo 'LOONGARCH64_RECORD_CHUNK=PASS full_word=2 full_pair=2 partial_tail=7 source_assembly_only=1'
