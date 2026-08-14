#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-enum-tag-type-references
assembly="$work/enum_tag_type_references.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/enum_tag_type_references.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$assembly"
test -s "$assembly"
grep -F 'normalize_state:' "$assembly" >/dev/null
grep -F 'timer_result:' "$assembly" >/dev/null
grep -F 'fs_value:' "$assembly" >/dev/null

cat >"$work/duplicate.c" <<'EOF'
enum duplicate_tag;
enum duplicate_tag { DUPLICATE_A };
enum duplicate_tag { DUPLICATE_B };
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/duplicate.c" -o "$work/duplicate.i"
if "$minic" -S "$work/duplicate.i" -o "$work/duplicate.s" 2>"$work/duplicate.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/enum_tag_type_references: duplicate completed enum accepted' >&2
    exit 1
fi
grep -F 'duplicate enum definition' "$work/duplicate.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/enum_tag_type_references lifecycle=incomplete-to-complete implicit-return=1 function-pointer-typedef=1 explicit-forward=1 record-function-pointer=1 stable-identity=1 compatible-representation=1 duplicate-definition=reject'
