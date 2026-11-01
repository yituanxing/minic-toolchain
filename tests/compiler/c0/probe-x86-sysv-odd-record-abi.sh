#!/usr/bin/env bash
# MC-082: GCC/MiniC bidirectional SysV INTEGER-class record ABI, 1..16 bytes.
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
fail() { echo "MC082_SYSV_RECORD_ABI=FAIL $*" >&2; exit 1; }
test -x "$minic" || fail 'MiniC binary missing'

for part in functions driver; do
    input="$root/tests/compiler/audit/mc082_sysv_record_abi_$part.c"
    "$cc" -std=gnu11 -E -P "$input" -o "$work/$part.i" ||
        fail "part=$part GNU preprocess"
    "$cc" -std=gnu11 -O0 -Wall -Wextra -Werror -Wno-misleading-indentation -fno-pie -x c \
        -c "$work/$part.i" -o "$work/$part.gnu.o" ||
        fail "part=$part GNU C compiler"
    "$minic" --target=x86_64-linux -S "$work/$part.i" \
        -o "$work/$part.minic.s" || fail "part=$part MiniC -S"
    "$cc" -x assembler -c "$work/$part.minic.s" \
        -o "$work/$part.minic.o" || fail "part=$part GNU AS MiniC output"
done

# Cross both sides of the ABI boundary (incoming, outgoing, function return).
# The 1..16B sizes exercise partial eightbytes and GP stack exhaustion; the
# packed5/align8/padded8/padded16 distinguish object extent from alignment.
for caller in gnu minic; do
    for callee in gnu minic; do
        "$cc" -no-pie "$work/driver.$caller.o" "$work/functions.$callee.o" \
            -o "$work/$caller-$callee" ||
            fail "link caller=$caller callee=$callee"
        status=0
        "$work/$caller-$callee" || status=$?
        test "$status" -eq 42 ||
            fail "native caller=$caller callee=$callee exit=$status expected=42"
        echo "MC082_SYSV_RECORD_ABI_MODE=PASS caller=$caller callee=$callee exit=42"
    done
done

# A packed unaligned scalar is SysV MEMORY, not an INTEGER register record.
# Until the separate <=16B MEMORY/sret transport is modeled, fail closed.
negative="$root/tests/compiler/audit/mc082_sysv_unaligned_memory_negative.c"
"$cc" -std=gnu11 -O0 -Werror -no-pie "$negative" -o "$work/unaligned.gnu" ||
    fail 'unaligned GNU native build'
status=0
"$work/unaligned.gnu" || status=$?
test "$status" -eq 42 || fail "unaligned GNU native exit=$status expected=42"
"$cc" -std=gnu11 -E -P "$negative" -o "$work/unaligned.i" ||
    fail 'unaligned GNU preprocess'
if "$minic" --target=x86_64-linux -S "$work/unaligned.i" \
        -o "$work/unaligned.minic.s" >"$work/unaligned.log" 2>&1; then
    fail 'unaligned packed MEMORY record wrongly admitted as INTEGER'
fi
if ! grep -Eq 'cannot allocate or verify Core function frame|Core terminator unsupported' \
        "$work/unaligned.log"; then
    fail 'unaligned packed MEMORY record failed for an unrelated reason'
fi
echo 'MC082_SYSV_RECORD_ABI=PASS sizes=1..16 packed_natural=5 align8=1 padded=8,16 stack_fallback=3 GNU_AS_ELF_native_cross=4 unaligned_MEMORY_fail_closed=1'
