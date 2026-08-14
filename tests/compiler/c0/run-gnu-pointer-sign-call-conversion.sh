#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-pointer-sign-call-conversion
assembly="$work/gnu_pointer_sign_call_conversion.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/gnu_pointer_sign_call_conversion.c" \
    -o "$work/gnu_pointer_sign_call_conversion.i"
"$minic" -S "$work/gnu_pointer_sign_call_conversion.i" -o "$assembly"

test -s "$assembly"
grep -F 'direct_pointer_sign_call:' "$assembly" >/dev/null
grep -F 'indirect_pointer_sign_call:' "$assembly" >/dev/null
grep -F 'call update_signed' "$assembly" >/dev/null

cat >"$work/assignment_mismatch.c" <<'EOF'
typedef unsigned int u32;
int reject_assignment(void) {
    u32 value = 1U;
    u32 *source = &value;
    int *target;
    target = source;
    return *target;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/assignment_mismatch.c" -o "$work/assignment_mismatch.i"
if "$minic" -S "$work/assignment_mismatch.i" -o "$work/assignment_mismatch.s" \
    >"$work/assignment_mismatch.stdout" 2>"$work/assignment_mismatch.stderr"; then
    printf '%s\n' 'pointer-sign ordinary assignment unexpectedly compiled' >&2
    exit 1
fi
grep -E 'assignment.*type|type.*assignment' "$work/assignment_mismatch.stderr" >/dev/null

cat >"$work/qualifier_loss.c" <<'EOF'
typedef unsigned int u32;
static int consume(int *value) { return *value; }
int reject_qualifier_loss(void) {
    const u32 value = 1U;
    return consume(&value);
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/qualifier_loss.c" -o "$work/qualifier_loss.i"
if "$minic" -S "$work/qualifier_loss.i" -o "$work/qualifier_loss.s" \
    >"$work/qualifier_loss.stdout" 2>"$work/qualifier_loss.stderr"; then
    printf '%s\n' 'pointer-sign call conversion unexpectedly discarded const' >&2
    exit 1
fi
grep -F 'call argument type does not match declaration' "$work/qualifier_loss.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/gnu_pointer_sign_call_conversion direct=1 indirect=1 one-level-integer-rank=1 ordinary-assignment=reject qualifier-loss=reject'
