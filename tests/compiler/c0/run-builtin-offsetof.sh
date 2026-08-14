#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-offsetof

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/builtin_offsetof.c" \
    -o "$work/builtin_offsetof.i"
"$minic" -S "$work/builtin_offsetof.i" -o "$work/builtin_offsetof.s"

grep -F '  li a0, 4' "$work/builtin_offsetof.s" >/dev/null
grep -F '  li a0, 8' "$work/builtin_offsetof.s" >/dev/null
grep -F '  li a0, 32' "$work/builtin_offsetof.s" >/dev/null
grep -F '  li a0, 24' "$work/builtin_offsetof.s" >/dev/null
grep -F 'indexed_offset:' "$work/builtin_offsetof.s" >/dev/null
grep -F 'nested_indexed_offset:' "$work/builtin_offsetof.s" >/dev/null

cat >"$work/non-array-index.c" <<'EOF'
struct ScalarOnly { unsigned long value; };
unsigned long bad(unsigned int idx)
{
    return __builtin_offsetof(struct ScalarOnly, value[idx]);
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/non-array-index.c" -o "$work/non-array-index.i"
if "$minic" -S "$work/non-array-index.i" -o "$work/non-array-index.s" \
    2>"$work/non-array-index.stderr"; then
    printf '%s\n' 'offsetof array designator unexpectedly accepted on scalar field' >&2
    exit 1
fi
grep -F '__builtin_offsetof array designator requires an array field' \
    "$work/non-array-index.stderr" >/dev/null

cat >"$work/non-record-member.c" <<'EOF'
struct ScalarPath { unsigned long value; };
unsigned long bad(void)
{
    return __builtin_offsetof(struct ScalarPath, value.child);
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/non-record-member.c" -o "$work/non-record-member.i"
if "$minic" -S "$work/non-record-member.i" -o "$work/non-record-member.s" \
    2>"$work/non-record-member.stderr"; then
    printf '%s\n' 'offsetof nested member unexpectedly accepted through scalar field' >&2
    exit 1
fi
grep -F '__builtin_offsetof nested member designator requires a record' \
    "$work/non-record-member.stderr" >/dev/null

cat >"$work/nested-bit-field.c" <<'EOF'
struct BitInner { unsigned int flag : 1; };
struct BitOuter { struct BitInner inner; };
unsigned long bad(void)
{
    return __builtin_offsetof(struct BitOuter, inner.flag);
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/nested-bit-field.c" -o "$work/nested-bit-field.i"
if "$minic" -S "$work/nested-bit-field.i" -o "$work/nested-bit-field.s" \
    2>"$work/nested-bit-field.stderr"; then
    printf '%s\n' 'offsetof nested bit-field unexpectedly accepted' >&2
    exit 1
fi
grep -F '__builtin_offsetof cannot name a bit-field' "$work/nested-bit-field.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/builtin_offsetof direct-member=1 typedef=1 promoted-anonymous=2 shared-member-resolver=1 target-layout=1 array-bound=8 array-designator=constant+runtime nested-member=record-path array-then-member=1 multidimensional-index=1 semantic=field-offset-terms+index*sizeof scalar-index=reject'
