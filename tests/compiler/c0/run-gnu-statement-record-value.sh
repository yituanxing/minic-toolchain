#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-statement-record-value
assembly="$work/gnu_statement_record_value.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_statement_record_value.c" \
    -o "$work/gnu_statement_record_value.i"
"$minic" -S "$work/gnu_statement_record_value.i" -o "$assembly"

test -s "$assembly"
grep -F 'construct_box:' "$assembly" >/dev/null
grep -F 'assign_box:' "$assembly" >/dev/null
grep -F '  call try_lock' "$assembly" >/dev/null
# Two RECORD_COPY sites must snapshot from address-backed statement-expression results.
copy_loads=$(grep -c '^  lbu t0, 0(t2)$' "$assembly")
copy_stores=$(grep -c '^  sb t0, 0(t3)$' "$assembly")
test "$copy_loads" -ge 16
test "$copy_stores" -ge 16

cat >"$work/not_lvalue.c" <<'EOF'
typedef struct item { long value; } item_t;
item_t *bad(void)
{
    return &({ item_t temporary = { .value = 1 }; temporary; });
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/not_lvalue.c" -o "$work/not_lvalue.i"
if "$minic" -S "$work/not_lvalue.i" -o "$work/not_lvalue.s" \
    >"$work/not_lvalue.stdout" 2>"$work/not_lvalue.stderr"; then
    printf '%s\n' 'statement-expression record value unexpectedly became an lvalue' >&2
    exit 1
fi
grep -F 'address-of requires an lvalue object or function designator'     "$work/not_lvalue.stderr" >/dev/null

cat >"$work/record_call.c" <<'EOF'
typedef struct item { long value; } item_t;
extern item_t make_item(void);
item_t still_bounded(void)
{
    item_t value = make_item();
    return value;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/record_call.c" -o "$work/record_call.i"
if "$minic" -S "$work/record_call.i" -o "$work/record_call.s" \
    >"$work/record_call.stdout" 2>"$work/record_call.stderr"; then
    printf '%s\n' 'record call rvalue unexpectedly entered address-backed RECORD_COPY path' >&2
    exit 1
fi
grep -F 'record local initializer requires a matching address-backed record value' \
    "$work/record_call.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/gnu_statement_record_value initializer=record-rvalue assignment=record-rvalue source=address-backed statement-expression lvalue=unchanged call-rvalue=bounded'
