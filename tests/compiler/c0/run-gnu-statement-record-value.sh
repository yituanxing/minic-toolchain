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
typedef struct pair { long first; long second; } pair_t;
typedef struct large { long first; long second; long third; } large_t;
extern item_t make_item(void);
extern pair_t make_pair(long value);
extern large_t make_large(void);

item_t initialize_from_call(void)
{
    item_t value = make_item();
    return value;
}

void assign_from_call(pair_t *target, long value)
{
    *target = make_pair(value);
}

pair_t infer_from_call(long value)
{
    __auto_type inferred = make_pair(value);
    return inferred;
}

void unsupported_large_call(large_t *target)
{
    *target = make_large();
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/record_call.c" -o "$work/record_call.i"
# The supported 8/16-byte forms must compile; keep the 24-byte target boundary separate.
sed '/void unsupported_large_call/,$d' "$work/record_call.i" >"$work/record_call_supported.i"
"$minic" -S "$work/record_call_supported.i" -o "$work/record_call.s"
grep -F '  call make_item' "$work/record_call.s" >/dev/null
grep -F '  call make_pair' "$work/record_call.s" >/dev/null
grep -F '  sd a0, 0(sp)' "$work/record_call.s" >/dev/null
grep -F '  sd a1, 8(sp)' "$work/record_call.s" >/dev/null
call_copy_loads=$(grep -c '^  lbu t0, 0(t2)$' "$work/record_call.s")
call_copy_stores=$(grep -c '^  sb t0, 0(t3)$' "$work/record_call.s")
test "$call_copy_loads" -ge 40
test "$call_copy_stores" -ge 40

cat >"$work/large_call.c" <<'EOF'
typedef struct large { long first; long second; long third; } large_t;
extern large_t make_large(void);
void unsupported_large_call(large_t *target)
{
    *target = make_large();
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/large_call.c" -o "$work/large_call.i"
if "$minic" -S "$work/large_call.i" -o "$work/large_call.s" \
    >"$work/large_call.stdout" 2>"$work/large_call.stderr"; then
    printf '%s\n' 'unsupported 24-byte record call copy unexpectedly succeeded' >&2
    exit 1
fi

printf '%s\n' \
    'PASS compiler/c0/gnu_statement_record_value initializer=record-rvalue assignment=record-rvalue address-backed=preserved call-rvalue=8+16-byte register-backed auto-type=1 large-call=bounded lvalue=unchanged'
