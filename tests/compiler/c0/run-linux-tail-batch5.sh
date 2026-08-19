#!/bin/sh
set -eu
# Batch5 isolates static aggregate materialization from unrelated runtime access lowering.
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/linux-tail-batch5
rm -rf "$work"
mkdir -p "$work"

cat >"$work/global-inferred.c" <<'SRC'
struct token { int id; const char *pattern; };
static const struct token tokens[] = {
    { 1, "one" },
    { 2, "two" },
};
SRC
"$minic" -S "$work/global-inferred.c" -o "$work/global-inferred.s"
grep -F '.size tokens, 32' "$work/global-inferred.s" >/dev/null
grep -F '__minic_str_' "$work/global-inferred.s" >/dev/null

cat >"$work/local-inferred.c" <<'SRC'
static int anchor;
struct row { int value; int *pointer; const char *name; };
int probe(void)
{
    static const struct row rows[] = {
        { 1, &anchor, "one" },
        { 2, &anchor, "two" },
    };
    return 0;
}
SRC
"$minic" -S "$work/local-inferred.c" -o "$work/local-inferred.s"
grep -F '.size __minic_static_local_' "$work/local-inferred.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/linux-tail-batch5 inferred-aggregate=global+static-local relocation-slot=logical+layout runtime-access=decoupled'
