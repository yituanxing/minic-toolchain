#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-block-scope-record-tags

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/block_scope_record_tags.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'block_scope_record_tags:' "$work/output.s" >/dev/null
grep -F 'main:' "$work/output.s" >/dev/null

cat >"$work/forward-shadow.c" <<'EOF'
struct shadow_tag { int outer; };
int forward_shadow(void) {
    {
        struct shadow_tag;
        return sizeof(struct shadow_tag);
    }
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/forward-shadow.c" -o "$work/forward-shadow.i"
if "$minic" -S "$work/forward-shadow.i" -o "$work/forward-shadow.s" \
    >"$work/forward-shadow.out" 2>"$work/forward-shadow.err"; then
    printf '%s\n' 'FAIL compiler/c0/block-scope-record-tags: block forward tag did not shadow outer complete tag' >&2
    exit 1
fi
grep -F 'incomplete' "$work/forward-shadow.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/block-scope-record-tags standalone=struct+union shadow=nested restore=outer forward=current-scope definition=reuse definition-with-declarator=preserved'
