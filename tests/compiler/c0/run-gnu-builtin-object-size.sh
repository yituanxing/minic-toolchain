#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-builtin-object-size
assembly="$work/gnu_builtin_object_size.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_builtin_object_size.c" \
    -o "$work/gnu_builtin_object_size.i"
"$minic" -S "$work/gnu_builtin_object_size.i" -o "$assembly"

test -s "$assembly"
grep -F 'unknown_max:' "$assembly" >/dev/null
grep -F 'unknown_subobject:' "$assembly" >/dev/null
grep -F 'unknown_min:' "$assembly" >/dev/null
grep -F 'known_array:' "$assembly" >/dev/null
grep -F '  li a0, -1' "$assembly" >/dev/null
grep -F '  li a0, 9' "$assembly" >/dev/null
if grep -F '__builtin_object_size' "$assembly" >/dev/null; then
    printf '%s\n' '__builtin_object_size leaked into emitted assembly' >&2
    exit 1
fi
if grep -F 'call side_effect_pointer' "$assembly" >/dev/null; then
    printf '%s\n' '__builtin_object_size evaluated its pointer operand at runtime' >&2
    exit 1
fi

cat >"$work/invalid_mode.c" <<'EOF'
unsigned long invalid_mode(void *pointer) {
    return __builtin_object_size(pointer, 4);
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/invalid_mode.c" -o "$work/invalid_mode.i"
if "$minic" -S "$work/invalid_mode.i" -o "$work/invalid_mode.s" \
    >"$work/invalid_mode.stdout" 2>"$work/invalid_mode.stderr"; then
    printf '%s\n' 'invalid __builtin_object_size mode unexpectedly compiled' >&2
    exit 1
fi
grep -F '__builtin_object_size mode must be between 0 and 3' \
    "$work/invalid_mode.stderr" >/dev/null

cat >"$work/nonconstant_mode.c" <<'EOF'
unsigned long nonconstant_mode(void *pointer, int mode) {
    return __builtin_object_size(pointer, mode);
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/nonconstant_mode.c" -o "$work/nonconstant_mode.i"
if "$minic" -S "$work/nonconstant_mode.i" -o "$work/nonconstant_mode.s" \
    >"$work/nonconstant_mode.stdout" 2>"$work/nonconstant_mode.stderr"; then
    printf '%s\n' 'nonconstant __builtin_object_size mode unexpectedly compiled' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/gnu_builtin_object_size query=compile-time unknown=max:-1,min:0 direct-array=9 side-effects=unevaluated mode=0..3'
