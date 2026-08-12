#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-function-attributes

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_function_attributes.c" \
    -o "$work/gnu_function_attributes.i"
"$minic" -S "$work/gnu_function_attributes.i" \
    -o "$work/gnu_function_attributes.s"

test -s "$work/gnu_function_attributes.s"
grep -F 'call_attribute_functions:' "$work/gnu_function_attributes.s" >/dev/null
grep -F '  call allocate_like' "$work/gnu_function_attributes.s" >/dev/null
grep -F '  call allocate_sized' "$work/gnu_function_attributes.s" >/dev/null
grep -F '  call allocate_matrix' "$work/gnu_function_attributes.s" >/dev/null
grep -F '  call allocate_aligned' "$work/gnu_function_attributes.s" >/dev/null
grep -F '  call allocate_aligned_offset' "$work/gnu_function_attributes.s" >/dev/null
grep -F '  call memory_copy' "$work/gnu_function_attributes.s" >/dev/null
grep -F '  call memory_compare' "$work/gnu_function_attributes.s" >/dev/null
grep -F '  call stable_transform' "$work/gnu_function_attributes.s" >/dev/null

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_function_attribute_reject.c" \
    -o "$work/gnu_function_attribute_reject.i"
set +e
"$minic" -S "$work/gnu_function_attribute_reject.i" \
    -o "$work/gnu_function_attribute_reject.s" \
    >"$work/reject.stdout" 2>"$work/reject.stderr"
status=$?
set -e
test "$status" -ne 0
grep -F 'unsupported GNU function attribute' "$work/reject.stderr" >/dev/null

for mode in missing too-many; do
    cpp_flags=
    if test "$mode" = too-many; then
        cpp_flags=-DTOO_MANY_ALLOC_SIZE_ARGUMENTS
    fi
    # shellcheck disable=SC2086
    "$host_cc" -E -P -x c $cpp_flags \
        "$root/tests/compiler/c0/gnu_function_attribute_argument_reject.c" \
        -o "$work/gnu_function_attribute_argument_reject-$mode.i"
    set +e
    "$minic" -S "$work/gnu_function_attribute_argument_reject-$mode.i" \
        -o "$work/gnu_function_attribute_argument_reject-$mode.s" \
        >"$work/argument-$mode.stdout" 2>"$work/argument-$mode.stderr"
    status=$?
    set -e
    test "$status" -ne 0
    grep -F 'GNU attribute has an invalid number of arguments' \
        "$work/argument-$mode.stderr" >/dev/null
done

for mode in missing too-many; do
    cat >"$work/assume-aligned-$mode.c" <<EOF
extern void *bad(void) __attribute__((__assume_aligned__($([ "$mode" = too-many ] && printf '8, 0, 1'))));
EOF
    "$host_cc" -E -P -x c "$work/assume-aligned-$mode.c" -o "$work/assume-aligned-$mode.i"
    set +e
    "$minic" -S "$work/assume-aligned-$mode.i" -o "$work/assume-aligned-$mode.s" \
        >"$work/assume-aligned-$mode.stdout" 2>"$work/assume-aligned-$mode.stderr"
    status=$?
    set -e
    test "$status" -ne 0
    grep -F 'GNU attribute has an invalid number of arguments' \
        "$work/assume-aligned-$mode.stderr" >/dev/null
done

printf '%s\n' 'PASS compiler/c0/gnu_function_attributes metadata=nothrow,leaf,nonnull,access,pure,malloc,alloc-size,assume-aligned,noreturn,deprecated,const-keyword arguments=registry-validated placement=pre-declarator,suffix optimization-metadata=parse-only unknown=reject aligned=not-silently-ignored'
