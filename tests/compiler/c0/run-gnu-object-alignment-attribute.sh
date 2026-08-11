#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-gnu-object-alignment-attribute"

mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_object_alignment_attribute.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"

awk '
    /^\.align 6$/ { align = 6; next }
    /^\.align 3$/ { align = 3; next }
    /^jiffies_64:$/ { if (align == 6) j64 = 1; next }
    /^jiffies:$/ { if (align == 6) j = 1; next }
    /^ordinary:$/ { if (align == 3) ordinary = 1; next }
    END { exit !(j64 && j && ordinary) }
' "$work/output.s"
test "$(grep -c '^\.section \.data\.\.cacheline_aligned$' "$work/output.s")" -eq 2

cat >"$work/invalid.c" <<'EOF'
extern int __attribute__((__aligned__(24))) invalid_alignment;
EOF
"$host_cc" -E -P -x c "$work/invalid.c" -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" 2>"$work/invalid.stderr"; then
    printf '%s\n' "FAIL compiler/c0/gnu_object_alignment_attribute: non-power-of-two alignment accepted" >&2
    exit 1
fi
grep -F "GNU object alignment must be a power of two" "$work/invalid.stderr" >/dev/null

printf '%s\n' "PASS compiler/c0/gnu_object_alignment_attribute ownership=object alignment=64 section=preserved type-contamination=none invalid=reject"
