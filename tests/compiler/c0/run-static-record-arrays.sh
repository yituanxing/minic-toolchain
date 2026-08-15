#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-record-array

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_record_array.c" \
    -o "$work/static_record_array.i"
"$minic" -S "$work/static_record_array.i" \
    -o "$work/static_record_array.s"

grep -Fx '.section .rodata' "$work/static_record_array.s" >/dev/null
grep -F '.type priority, @object' "$work/static_record_array.s" >/dev/null
grep -F '.size priority, 6' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 10' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 6' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 4' "$work/static_record_array.s" >/dev/null
if grep -F '.globl priority' "$work/static_record_array.s" >/dev/null; then
    echo 'static record array leaked external linkage' >&2
    exit 1
fi
grep -F 'read_priority:' "$work/static_record_array.s" >/dev/null
grep -F '.type sched_core_sysctls_like, @object' "$work/static_record_array.s" >/dev/null
grep -F '.size sched_core_sysctls_like, 32' "$work/static_record_array.s" >/dev/null
if grep -F '.globl sched_core_sysctls_like' "$work/static_record_array.s" >/dev/null; then
    echo 'complex static record array leaked external linkage' >&2
    exit 1
fi
grep -F 'read_sysctl_size:' "$work/static_record_array.s" >/dev/null
grep -F '.type named_hooks, @object' "$work/static_record_array.s" >/dev/null
grep -F '.size named_hooks, 24' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 115' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 104' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 97' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 114' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 101' "$work/static_record_array.s" >/dev/null
grep -F '  .dword read_named' "$work/static_record_array.s" >/dev/null
grep -F '  .dword write_named' "$work/static_record_array.s" >/dev/null
grep -F '.type exact_tags, @object' "$work/static_record_array.s" >/dev/null
grep -F '.size exact_tags, 8' "$work/static_record_array.s" >/dev/null

cat >"$work/too_long.c" <<'EOF'
struct TooLongName {
    char name[3];
};
static struct TooLongName bad[] = {
    { .name = "abcd" },
};
EOF
"$host_cc" -E -P -x c "$work/too_long.c" -o "$work/too_long.i"
if "$minic" -S "$work/too_long.i" -o "$work/too_long.s" 2>"$work/too_long.err"; then
    echo 'overlong fixed character-array field string initializer was accepted' >&2
    exit 1
fi
grep -F 'string initializer is too long for character array' "$work/too_long.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/static_record_array inferred-count=3 fields=2 missing-field-zero=1 size=6 complex-empty=1 complex-size=32 string-field=1 exact-fit=1 function-relocations=1 shared-owner=1 internal-rodata=1'
