#!/usr/bin/env bash
# GNU-preprocess a small LoongArch source. Verify the direct frame transport
# and both correctness fallbacks without needing a cross assembler on hosts.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "LOONG_FRAME_FOLD=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC compiler missing'
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/loongarch64_frame_fold.c" \
    -o "$work/source.i"
"$minic" --target=loongarch64-linux -S "$work/source.i" \
    -o "$work/source.s" || fail 'MiniC LoongArch compilation'

awk '/^work:$/ {seen=1} seen {print} /^\.size work,/ {exit}' \
    "$work/source.s" > "$work/work.s"
awk '/^alias_escape:$/ {seen=1} seen {print} /^\.size alias_escape,/ {exit}' \
    "$work/source.s" > "$work/escape.s"
awk '/^packed_read:$/ {seen=1} seen {print} /^\.size packed_read,/ {exit}' \
    "$work/source.s" > "$work/packed.s"

# Before direct frame folding this function occupied well over 200 assembly
# lines due to materialized frame-address chains and guarded scalar accesses.
test "$(wc -l < "$work/work.s")" -lt 150 || fail 'frame code-size regression'
grep -Eq '^[[:space:]]+st\.d \$t[0-9], \$sp, [0-9]+' \
    "$work/work.s" || fail 'direct frame scalar store missing'

# The object address must still exist when C makes it escape to a call.
grep -Fxq '  bl escape' "$work/escape.s" || fail 'pointer escape call missing'
grep -Eq '^[[:space:]]+(add|addi)\.d [^,]+, \$sp,' \
    "$work/escape.s" || fail 'escaped local frame address missing'

# Packed external pointers have no proven alignment. Keep the byte fallback.
grep -Fq '  andi $t3, $t1, 3' "$work/packed.s" \
    || fail 'unaligned 32-bit guard missing'
grep -Fq '  ld.bu $t2, $t1, 0' "$work/packed.s" \
    || fail 'unaligned byte-gather missing'

printf 'LOONG_FRAME_FOLD=PASS work_lines=%s escape_lines=%s packed_lines=%s\n' \
    "$(wc -l < "$work/work.s")" "$(wc -l < "$work/escape.s")" \
    "$(wc -l < "$work/packed.s")"
