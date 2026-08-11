#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-extern-object-redeclaration
assembly="$work/gnu_extern_object_redeclaration.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_extern_object_redeclaration.c" \
    -o "$work/gnu_extern_object_redeclaration.i"
"$minic" -S "$work/gnu_extern_object_redeclaration.i" -o "$assembly"

test -s "$assembly"
grep -F 'read_redeclarations:' "$assembly" >/dev/null
grep -F '  la a0, repeated_scalar' "$assembly" >/dev/null
grep -F '  la a0, repeated_const' "$assembly" >/dev/null
grep -F '  la a0, repeated_incomplete' "$assembly" >/dev/null
grep -F '  la a0, completed_array' "$assembly" >/dev/null
grep -F '  la a0, fixed_array' "$assembly" >/dev/null
grep -F '.globl defined_object' "$assembly" >/dev/null
grep -F '.size defined_object, 4' "$assembly" >/dev/null
if grep -E '^\.globl (repeated_scalar|repeated_const|repeated_incomplete|completed_array|fixed_array|repeated_record)$' \
    "$assembly" >/dev/null; then
    printf '%s\n' 'extern-only redeclaration unexpectedly emitted a definition' >&2
    exit 1
fi
# sizeof(completed_array)==16 and sizeof(fixed_array)==12 prove the canonical declaration type
# absorbed compatible bound information instead of keeping the first incomplete descriptor.
grep -F '  li a0, 16' "$assembly" >/dev/null
grep -F '  li a0, 12' "$assembly" >/dev/null

cat >"$work/type_conflict.c" <<'EOF'
extern int conflict;
extern unsigned int conflict;
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/type_conflict.c" -o "$work/type_conflict.i"
if "$minic" -S "$work/type_conflict.i" -o "$work/type_conflict.s" \
    >"$work/type_conflict.stdout" 2>"$work/type_conflict.stderr"; then
    printf '%s\n' 'incompatible extern scalar redeclaration unexpectedly compiled' >&2
    exit 1
fi
grep -F 'conflicting extern object redeclaration' "$work/type_conflict.stderr" >/dev/null

cat >"$work/bound_conflict.c" <<'EOF'
extern int conflict_array[4];
extern int conflict_array[5];
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/bound_conflict.c" -o "$work/bound_conflict.i"
if "$minic" -S "$work/bound_conflict.i" -o "$work/bound_conflict.s" \
    >"$work/bound_conflict.stdout" 2>"$work/bound_conflict.stderr"; then
    printf '%s\n' 'incompatible extern array redeclaration unexpectedly compiled' >&2
    exit 1
fi
grep -F 'conflicting extern object redeclaration' "$work/bound_conflict.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/gnu_extern_object_redeclaration scalar=merge const=merge record=merge array=transaction+composite-bound definition-then-extern=merge incompatible=reject'
