#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-array-range-initializer

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_array_range_initializer.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'runtime_array_range_initializer:' "$work/output.s" >/dev/null
grep -F 'direct_local_array_range_initializer:' "$work/output.s" >/dev/null
grep -F 'direct_index_nonconstant:' "$work/output.s" >/dev/null
grep -F 'single_element_range_effect:' "$work/output.s" >/dev/null
grep -F '  call range_effect' "$work/output.s" >/dev/null

cat >"$work/nonconstant-range.c" <<'EOF_CASE'
int nonconstant_range(int value)
{
    int items[2] = { [0 ... 1] = value };
    return items[0] + items[1];
}
EOF_CASE
"$host_cc" -E -P -std=gnu11 -x c "$work/nonconstant-range.c" -o "$work/nonconstant-range.i"
if "$minic" -S "$work/nonconstant-range.i" -o "$work/nonconstant-range.s" \
  >"$work/nonconstant-range.stdout" 2>"$work/nonconstant-range.stderr"; then
  printf '%s\n' 'FAIL compiler/c0/gnu-array-range-initializer: nonconstant range value accepted' >&2
  exit 1
fi
grep -F 'multi-element runtime array range initializer requires an integer constant value' \
  "$work/nonconstant-range.stderr" >/dev/null

cat >"$work/reversed-range.c" <<'EOF_CASE'
int reversed_range(void)
{
    int items[2] = { [1 ... 0] = 3 };
    return items[0];
}
EOF_CASE
"$host_cc" -E -P -std=gnu11 -x c "$work/reversed-range.c" -o "$work/reversed-range.i"
if "$minic" -S "$work/reversed-range.i" -o "$work/reversed-range.s" \
  >"$work/reversed-range.stdout" 2>"$work/reversed-range.stderr"; then
  printf '%s\n' 'FAIL compiler/c0/gnu-array-range-initializer: reversed range accepted' >&2
  exit 1
fi
grep -F 'GNU array range designator upper bound is below lower bound' \
  "$work/reversed-range.stderr" >/dev/null

cat >"$work/out-of-range.c" <<'EOF_CASE'
int out_of_range(void)
{
    int items[2] = { [2] = 3 };
    return items[0];
}
EOF_CASE
"$host_cc" -E -P -std=gnu11 -x c "$work/out-of-range.c" -o "$work/out-of-range.i"
if "$minic" -S "$work/out-of-range.i" -o "$work/out-of-range.s" \
  >"$work/out-of-range.stdout" 2>"$work/out-of-range.stderr"; then
  printf '%s\n' 'FAIL compiler/c0/gnu-array-range-initializer: out-of-range designator accepted' >&2
  exit 1
fi
grep -F 'array designator index is outside the initialized array' \
  "$work/out-of-range.stderr" >/dev/null

cat >"$work/backward.c" <<'EOF_CASE'
int backward_designator(void)
{
    int items[3] = { [1] = 3, [0] = 4 };
    return items[0];
}
EOF_CASE
"$host_cc" -E -P -std=gnu11 -x c "$work/backward.c" -o "$work/backward.i"
if "$minic" -S "$work/backward.i" -o "$work/backward.s" \
  >"$work/backward.stdout" 2>"$work/backward.stderr"; then
  printf '%s\n' 'FAIL compiler/c0/gnu-array-range-initializer: backward designator accepted' >&2
  exit 1
fi
grep -F 'backward runtime array designators are not supported yet' \
  "$work/backward.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu-array-range-initializer index=C99 range=GNU fixed-runtime-owner=shared nested-compound=linux-shape multi-range-constant=1 single-range-runtime=1 nonconstant-index=1 forward-only=1 bounds=checked'
