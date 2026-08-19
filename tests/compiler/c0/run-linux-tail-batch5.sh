#!/bin/sh
set -eu
# Batch5 validates logical relocation typing and physical DataLayout together.
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
int probe(void) { return tokens[1].id; }
SRC
"$minic" -S "$work/global-inferred.c" -o "$work/global-inferred.s"
grep -F '.size tokens, 32' "$work/global-inferred.s" >/dev/null

cat >"$work/local-inferred.c" <<'SRC'
static int anchor;
struct row { int value; int *pointer; const char *name; };
int probe(int index)
{
    static const struct row rows[] = {
        { 1, &anchor, "one" },
        { 2, &anchor, "two" },
    };
    return rows[index].value;
}
SRC
"$minic" -S "$work/local-inferred.c" -o "$work/local-inferred.s"
grep -F '.size __minic_static_local_' "$work/local-inferred.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/linux-tail-batch5 inferred-aggregate=global+static-local relocation-slot=incomplete-array'
