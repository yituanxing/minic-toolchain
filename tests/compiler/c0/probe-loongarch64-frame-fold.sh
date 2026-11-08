#!/usr/bin/env bash
# GNU-preprocess a small LoongArch source. Verify the *production typed
# MachineIR* frame transport and guarded memory paths in emitted assembly;
# the obsolete isolated FILE* frame-emitter fixture has been removed.
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
awk '/^large_frame_touch:$/ {seen=1} seen {print} /^\.size large_frame_touch,/ {exit}' \
    "$work/source.s" > "$work/large.s"

# The old standalone frame FILE* helper was not on the production path.
# Production MachineIR currently emits 205 lines for this fixture: keep
# that actual byte-checked baseline as a hard code-size ceiling while
# validating the real `ldptr` and guarded unaligned paths below.
test "$(wc -l < "$work/work.s")" -le 205 || fail 'frame code-size regression'
grep -Eq '^[[:space:]]+st\.d \$t[0-9], \$sp, [0-9]+' \
    "$work/work.s" || fail 'direct frame scalar store missing'

# The object address must still exist when C makes it escape to a call.
grep -Fxq '  bl escape' "$work/escape.s" || fail 'pointer escape call missing'
grep -Eq '^[[:space:]]+(add|addi)\.d [^,]+, \$sp,' \
    "$work/escape.s" || fail 'escaped local frame address missing'

# Packed external pointers have no proven alignment. Keep the byte fallback.
grep -Fq '  andi $t3, $t1, 3' "$work/packed.s" \
    || fail 'unaligned 32-bit guard missing'
# Typed MachineIR constructs the byte-gather slow path as an explicit CFG
# block: the PC-relative continuation must be immediately followed by the
# selected branch, and the slow block must return through that continuation.
awk '/^  pcaddi \$t6, 2$/ {getline; if ($0 ~ /^  b \.Lminic_mir_packed_read_[0-9]+$/) found=1}
     END {exit !found}' "$work/packed.s" \
    || fail 'packed access does not branch to typed byte-gather helper'
grep -Fq '  ld.bu $t2, $t1, 0' "$work/source.s" \
    || fail 'unaligned byte-gather helper missing'
grep -Fq '  jirl $zero, $t6, 0' "$work/packed.s" \
    || fail 'typed byte-gather continuation missing'

# The large object is address-taken. The typed selector therefore preserves
# the exact base+materialized-offset addressing instead of pretending that
# each access is an independently foldable frame load/store. Verify the
# checked prologue size and both positive/negative stack adjustments.
grep -Fq '  li.d $t3, 4144' "$work/large.s" \
    || fail 'large frame allocation immediate missing'
grep -Fq '  sub.d $sp, $sp, $t3' "$work/large.s" \
    || fail 'large frame allocation missing'
grep -Fq '  add.d $sp, $sp, $t3' "$work/large.s" \
    || fail 'large frame restoration missing'
# The typed selector materializes the small local-base offset directly with
# ADDI.D before storing the escaping address in its large (>12-bit) frame
# slot. The old `add.d $t0,$sp,$t0` spelling is not the ABI requirement.
awk '/^  addi\.d \$t0, \$sp, [0-9]+$/ {
         if (getline > 0 && $0 ~ /^  stptr\.d \$t0, \$sp, [0-9]+$/)
             found++
     }
     END { exit !(found >= 2) }' "$work/large.s" \
    || fail 'large frame object address missing'

printf 'LOONG_FRAME_FOLD=PASS work_lines=%s escape_lines=%s packed_lines=%s\n' \
    "$(wc -l < "$work/work.s")" "$(wc -l < "$work/escape.s")" \
    "$(wc -l < "$work/packed.s")"
