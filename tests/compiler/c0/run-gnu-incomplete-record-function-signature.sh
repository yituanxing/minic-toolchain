#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-incomplete-record-function-signature

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_incomplete_record_function_signature.c" \
  -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

cat >"$work/incomplete-return-definition.c" <<'EOF'
struct Pending;
struct Pending make_pending(void) { }
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/incomplete-return-definition.c" \
  -o "$work/incomplete-return-definition.i"
if "$minic" -S "$work/incomplete-return-definition.i" \
    -o "$work/incomplete-return-definition.s" \
    2>"$work/incomplete-return-definition.stderr"; then
  printf '%s\n' 'FAIL compiler/c0/gnu_incomplete_record_function_signature: incomplete return definition accepted' >&2
  exit 1
fi
grep -F 'function definition requires a complete return type' \
  "$work/incomplete-return-definition.stderr" >/dev/null

cat >"$work/incomplete-parameter-definition.c" <<'EOF'
struct Pending;
void consume_pending(struct Pending value) { }
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/incomplete-parameter-definition.c" \
  -o "$work/incomplete-parameter-definition.i"
if "$minic" -S "$work/incomplete-parameter-definition.i" \
    -o "$work/incomplete-parameter-definition.s" \
    2>"$work/incomplete-parameter-definition.stderr"; then
  printf '%s\n' 'FAIL compiler/c0/gnu_incomplete_record_function_signature: incomplete parameter definition accepted' >&2
  exit 1
fi
grep -F 'function definition requires complete object parameter types' \
  "$work/incomplete-parameter-definition.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_incomplete_record_function_signature declaration-return=1 declaration-parameter=1 function-pointer=1 completion=same-record-id definition-return=complete-required definition-parameter=complete-required'
