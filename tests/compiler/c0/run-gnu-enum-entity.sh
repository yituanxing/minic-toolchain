#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-enum-entity
assembly="$work/gnu_enum_entity.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_enum_entity.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$assembly"
test -s "$assembly"
grep -F 'forward_value:' "$assembly" >/dev/null
grep -F 'mm_state_test:' "$assembly" >/dev/null
grep -F 'wide_enum_test:' "$assembly" >/dev/null
grep -F '4294967295' "$assembly" >/dev/null
grep -F -- '-4294967296' "$assembly" >/dev/null

cat >"$work/duplicate.c" <<'EOF'
enum Duplicate;
enum Duplicate { D0 };
enum Duplicate { D1 };
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/duplicate.c" -o "$work/duplicate.i"
if "$minic" -S "$work/duplicate.i" -o "$work/duplicate.s" 2>"$work/duplicate.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/gnu_enum_entity: duplicate enum definition accepted' >&2
    exit 1
fi
grep -F 'duplicate enum definition' "$work/duplicate.stderr" >/dev/null

cat >"$work/incomplete-object.c" <<'EOF'
enum IncompleteObject;
enum IncompleteObject object;
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/incomplete-object.c" -o "$work/incomplete-object.i"
if "$minic" -S "$work/incomplete-object.i" -o "$work/incomplete-object.s" 2>"$work/incomplete-object.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/gnu_enum_entity: incomplete enum object storage accepted' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/gnu_enum_entity program-owned=1 stable-enum-id=1 forward-completion=1 typed-bits=1 uint32=1 ulong64=1 mixed-long=1 compatible-type=1 distinct-enum=1 incomplete-object=reject duplicate=reject'
