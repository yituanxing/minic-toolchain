#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/linux-tail-batch8
rm -rf "$work"
mkdir -p "$work"

# Keep this gate limited to the shared semantics targeted by batch8.
cat >"$work/local-pointer-to-array.c" <<'SRC'
int probe(void)
{
    unsigned char (*tags)[8];
    return sizeof(tags) == sizeof(void *);
}
SRC
"$minic" -S "$work/local-pointer-to-array.c" -o "$work/local-pointer-to-array.s"
grep -F '.globl probe' "$work/local-pointer-to-array.s" >/dev/null

cat >"$work/named-parameter-function-pointer-type.c" <<'SRC'
struct map { int value; };
int probe(void)
{
    return (int)sizeof((void *(*)(struct map *map, void *key))0);
}
SRC
"$minic" -S "$work/named-parameter-function-pointer-type.c" -o "$work/named-parameter-function-pointer-type.s"
grep -F '.globl probe' "$work/named-parameter-function-pointer-type.s" >/dev/null

cat >"$work/null-pointer-integer-case.c" <<'SRC'
struct item { int value; };
int probe(int x)
{
    switch (x) {
    case (unsigned long)((struct item *)0):
        return 1;
    default:
        return 0;
    }
}
SRC
"$minic" -S "$work/null-pointer-integer-case.c" -o "$work/null-pointer-integer-case.s"
grep -F '.globl probe' "$work/null-pointer-integer-case.s" >/dev/null

cat >"$work/incomplete-array-pointer-relational.c" <<'SRC'
struct row { int value; };
extern struct row __start_rows[];
extern struct row __stop_rows[];
int probe(void)
{
    return &__stop_rows > &__start_rows;
}
SRC
"$minic" -S "$work/incomplete-array-pointer-relational.c" -o "$work/incomplete-array-pointer-relational.s"
grep -F '.globl probe' "$work/incomplete-array-pointer-relational.s" >/dev/null

cat >"$work/gnu-void-pointer-relational.c" <<'SRC'
int probe(void *entry, void *limit)
{
    return entry >= limit && entry < limit + 32;
}
SRC
"$minic" -S "$work/gnu-void-pointer-relational.c" -o "$work/gnu-void-pointer-relational.s"
grep -F '.globl probe' "$work/gnu-void-pointer-relational.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/linux-tail-batch8 pointer-to-array=local function-pointer-type=leading-return-pointer null-pointer-integer-ice=zero relational=incomplete-object+gnu-void'
