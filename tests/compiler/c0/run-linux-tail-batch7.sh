#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/linux-tail-batch7
rm -rf "$work"
mkdir -p "$work"

cat >"$work/local-pointer-attr.c" <<'SRC'
struct task { int value; };
int probe(void)
{
    struct task * __attribute__((__unused__)) task = (struct task *)0;
    return task == (struct task *)0;
}
SRC
"$minic" -S "$work/local-pointer-attr.c" -o "$work/local-pointer-attr.s"
grep -F '.globl probe' "$work/local-pointer-attr.s" >/dev/null

cat >"$work/block-enum.c" <<'SRC'
int probe(void)
{
    enum mode { M_NONE, M_PARTIAL, M_FREE };
    enum mode mode = M_FREE;
    return mode;
}
SRC
"$minic" -S "$work/block-enum.c" -o "$work/block-enum.s"
grep -F '.globl probe' "$work/block-enum.s" >/dev/null

cat >"$work/parenthesized-field.c" <<'SRC'
struct fanotify_fh { int value; };
struct event {
    struct fanotify_fh (object_fh);
    unsigned char inline_buf[12];
};
int probe(struct event *event)
{
    return event->object_fh.value;
}
SRC
"$minic" -S "$work/parenthesized-field.c" -o "$work/parenthesized-field.s"
grep -F '.globl probe' "$work/parenthesized-field.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/linux-tail-batch7 local-pointer-attr=interposed block-type-only=enum parenthesized-field=direct-declarator'
