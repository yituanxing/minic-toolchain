#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-static-local-implicit-zero
assembly="$work/gnu_static_local_implicit_zero.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_static_local_implicit_zero.c" \
    -o "$work/gnu_static_local_implicit_zero.i"
"$minic" -S "$work/gnu_static_local_implicit_zero.i" -o "$assembly"

test -s "$assembly"
grep -F 'linux_static_key_shape:' "$assembly" >/dev/null
# Validate storage by object size instead of relying on a particular function id.
grep -E '^\.size __minic_static_local_[0-9]+_0, 16$' "$assembly" >/dev/null
grep -E '^\.size __minic_static_local_[0-9]+_1, 4$' "$assembly" >/dev/null
grep -E '^\.size __minic_static_local_[0-9]+_2, 8$' "$assembly" >/dev/null
grep -E '^\.size __minic_static_local_[0-9]+_3, 12$' "$assembly" >/dev/null
# Each backing definition is emitted as static-storage zero fill; initialization does not appear
# as runtime stores in linux_static_key_shape.
awk '
    /^__minic_static_local_[0-9]+_0:$/ { key=1; next }
    key && /^  \.zero 16$/ { key_zero=1; key=0 }
    /^__minic_static_local_[0-9]+_1:$/ { scalar=1; next }
    scalar && /^  \.zero 4$/ { scalar_zero=1; scalar=0 }
    /^__minic_static_local_[0-9]+_2:$/ { pointer=1; next }
    pointer && /^  \.zero 8$/ { pointer_zero=1; pointer=0 }
    /^__minic_static_local_[0-9]+_3:$/ { array=1; next }
    array && /^  \.zero 12$/ { array_zero=1; array=0 }
    END { exit key_zero && scalar_zero && pointer_zero && array_zero ? 0 : 1 }
' "$assembly"
grep -F '  call consume_address' "$assembly" >/dev/null

cat >"$work/incomplete_record.c" <<'EOF'
struct incomplete;
void bad_record(void)
{
    static struct incomplete value;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/incomplete_record.c" -o "$work/incomplete_record.i"
if "$minic" -S "$work/incomplete_record.i" -o "$work/incomplete_record.s" \
    >"$work/incomplete_record.stdout" 2>"$work/incomplete_record.stderr"; then
    printf '%s\n' 'incomplete implicit-zero static record unexpectedly compiled' >&2
    exit 1
fi
grep -F 'static local object without an initializer requires a complete object type' \
    "$work/incomplete_record.stderr" >/dev/null

cat >"$work/incomplete_array.c" <<'EOF'
void bad_array(void)
{
    static int values[];
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/incomplete_array.c" -o "$work/incomplete_array.i"
if "$minic" -S "$work/incomplete_array.i" -o "$work/incomplete_array.s" \
    >"$work/incomplete_array.stdout" 2>"$work/incomplete_array.stderr"; then
    printf '%s\n' 'incomplete implicit-zero static array unexpectedly compiled' >&2
    exit 1
fi
grep -F "expected '=' after inferred array" "$work/incomplete_array.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/gnu_static_local_implicit_zero record=zero scalar=zero pointer=zero fixed-array=existing-zero runtime-init=none incomplete=reject'
