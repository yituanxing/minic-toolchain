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
"${HOST_CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$root/src" \
    "$root/tests/compiler/c0/loongarch64_frame_ptr_offset.c" \
    "$root/src/target/loongarch64/frame_emit.c" \
    -o "$work/frame-ptr-offset"
"$work/frame-ptr-offset" > "$work/frame-ptr-offset.s" \
    || fail 'extended LoongArch frame immediate emitter'
grep -Fxq '  ldptr.d $t0, $sp, 2048' "$work/frame-ptr-offset.s" \
    || fail '2048B aligned double load must use LDPTR'
grep -Fxq '  stptr.d $t1, $sp, 32760' "$work/frame-ptr-offset.s" \
    || fail 'largest aligned double store must use STPTR'
grep -Fxq '  ldptr.w $t2, $sp, 2052' "$work/frame-ptr-offset.s" \
    || fail 'aligned signed word load must use LDPTR.W'
grep -Fxq '  stptr.w $t4, $s0, 32764' "$work/frame-ptr-offset.s" \
    || fail 'dynamic frame word store must use STPTR.W'
grep -Fxq '  ldptr.d $t5, $s0, 4096' "$work/frame-ptr-offset.s" \
    || fail 'dynamic frame double load must use LDPTR.D'
grep -Fxq '  ld.wu $t0, $t3, 0' "$work/frame-ptr-offset.s" \
    || fail 'unsigned word load lost its non-pointer fallback'
grep -Fxq '  ld.d $t1, $t3, 0' "$work/frame-ptr-offset.s" \
    || fail 'unaligned double load lost its non-pointer fallback'
grep -Fxq '  ld.d $t2, $t3, 0' "$work/frame-ptr-offset.s" \
    || fail '32768B double load lost its non-pointer fallback'
grep -Fxq '  addi.d $sp, $sp, -16' "$work/frame-ptr-offset.s" \
    || fail 'small frame allocation still materializes an immediate'
grep -Fxq '  addi.d $sp, $sp, 16' "$work/frame-ptr-offset.s" \
    || fail 'small frame restoration still materializes an immediate'
grep -Fxq '  addi.d $sp, $sp, -2048' "$work/frame-ptr-offset.s" \
    || fail 'signed immediate lower boundary lost'
grep -Fxq '  add.d $sp, $sp, $t3' "$work/frame-ptr-offset.s" \
    || fail 'positive 2048B frame adjustment must use wide fallback'
grep -Fxq '  addi.d $t0, $sp, 0' "$work/frame-ptr-offset.s" \
    || fail 'zero-offset object address must use one instruction'
grep -Fxq '  addi.d $t1, $s0, 2047' "$work/frame-ptr-offset.s" \
    || fail 'dynamic frame address at signed imm12 maximum not folded'
grep -Fxq '  add.d $t2, $sp, $t2' "$work/frame-ptr-offset.s" \
    || fail 'large object frame address lost its checked fallback'
if command -v loongarch64-linux-gnu-as >/dev/null 2>&1; then
    loongarch64-linux-gnu-as "$work/frame-ptr-offset.s" -o "$work/frame-ptr-offset.o" \
        || fail 'GNU AS rejected extended LoongArch frame operands'
fi
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
awk '/^large_frame_touch:$/ {seen=1} seen {print} /^\.size large_frame_touch,/ {exit}' \
    "$work/source.s" > "$work/large.s"

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

# The real generated function must actually use the extended register+offset
# addressing, not merely pass an isolated helper-output test.
grep -Eq '^[[:space:]]+(ldptr|stptr)\.[wd] \$t[0-9], \$sp, [0-9]+' \
    "$work/large.s" || fail 'large frame does not use pointer-offset machine forms'

printf 'LOONG_FRAME_FOLD=PASS work_lines=%s escape_lines=%s packed_lines=%s\n' \
    "$(wc -l < "$work/work.s")" "$(wc -l < "$work/escape.s")" \
    "$(wc -l < "$work/packed.s")"
