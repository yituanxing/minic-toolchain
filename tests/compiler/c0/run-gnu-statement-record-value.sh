#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-statement-record-value
assembly="$work/gnu_statement_record_value.s"

# Verify every byte copy as a load followed immediately by a store of the
# same byte at the same offset through distinct source/destination pointers.
# Register allocation and loop/unroll choice belong to the target backend.
check_byte_copy_pairs() {
    awk -v minimum="$2" '
        $1 == "lbu" {
            byte_register = $2
            sub(/,$/, "", byte_register)
            split($3, source, /[()]/)
            if ((getline <= 0) || $1 != "sb") exit 1
            store_register = $2
            sub(/,$/, "", store_register)
            split($3, destination, /[()]/)
            if (store_register != byte_register ||
                source[1] != destination[1] ||
                source[2] == destination[2]) exit 1
            paired++
        }
        END { if (paired < minimum) exit 1 }
    ' "$1"
}

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_statement_record_value.c" \
    -o "$work/gnu_statement_record_value.i"
"$minic" -S "$work/gnu_statement_record_value.i" -o "$assembly"

test -s "$assembly"
grep -F 'construct_box:' "$assembly" >/dev/null
grep -F 'assign_box:' "$assembly" >/dev/null
grep -F '  call try_lock' "$assembly" >/dev/null
grep -F '.type discard_record_lvalue, @function' "$assembly" >/dev/null
grep -F '  call discard_record_source' "$assembly" >/dev/null
grep -F '.type discard_zero_record_member, @function' "$assembly" >/dev/null
# Two RECORD_COPY sites must snapshot from address-backed statement-expression results.
check_byte_copy_pairs "$assembly" 16

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

void assign_from_large_call(large_t *target)
{
    *target = make_large();
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/record_call.c" -o "$work/record_call.i"
# Keep the register-return and indirect-return call-rvalue paths visible in one focused contract.
"$minic" -S "$work/record_call.i" -o "$work/record_call.s"
grep -F '  call make_item' "$work/record_call.s" >/dev/null
grep -F '  call make_pair' "$work/record_call.s" >/dev/null
grep -F '  call make_large' "$work/record_call.s" >/dev/null
# RV64 small-record ABI returns one word in a0; a 16-byte record returns two
# consecutive words in a0/a1. Their Core spill offsets may move, but each
# call result must be captured immediately in one contiguous stack object.
awk '
    /call make_item/ { expecting = "small"; next }
    /call make_pair/ { expecting = "pair"; next }
    expecting == "small" && $1 == "sd" && $2 == "a0," {
        split($3, slot, /[()]/)
        if (slot[2] == "sp" && slot[1] ~ /^-?[0-9]+$/) {
            captured_small++
            expecting = ""
        }
    }
    expecting == "pair" && $1 == "sd" && $2 == "a0," {
        split($3, first, /[()]/)
        if (first[2] != "sp" || first[1] !~ /^-?[0-9]+$/) exit 1
        if (getline <= 0 || $1 != "sd" || $2 != "a1,") exit 1
        split($3, second, /[()]/)
        if (second[2] != "sp" || second[1] != first[1] + 8) exit 1
        captured_pair++
        expecting = ""
    }
    END { if (captured_small < 1 || captured_pair < 2) exit 1 }
' "$work/record_call.s"
check_byte_copy_pairs "$work/record_call.s" 64

# Compare native behavior of the same GNU-preprocessed source against GCC.
runtime="$root/tests/compiler/c0/gnu_statement_record_value_runtime.c"
"$host_cc" -O0 -fno-pie -no-pie -x c "$work/gnu_statement_record_value.i" \
    "$runtime" -o "$work/gnu-native"
"$minic" --target=x86_64-linux -S "$work/gnu_statement_record_value.i" \
    -o "$work/gnu_statement_record_value.x86.s"
"$host_cc" -fno-pie -no-pie -x assembler "$work/gnu_statement_record_value.x86.s" \
    -x c "$runtime" -o "$work/minic-native"
"$work/gnu-native"
"$work/minic-native"

printf '%s\n' \
    'PASS compiler/c0/gnu_statement_record_value initializer=record-rvalue assignment=record-rvalue address-backed=preserved discard=record-lvalue+zero-member call-rvalue=8+16-register+24-indirect auto-type=1 hidden-result=1 lvalue=unchanged GNU_native_diff=PASS'
