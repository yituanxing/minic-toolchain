#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/linux-tail-batch4
rm -rf "$work"
mkdir -p "$work"

cat >"$work/pragma.c" <<'SRC'
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
static int value = 3;
#pragma GCC diagnostic warning "-Wformat"
#pragma GCC diagnostic pop
int main(void) { return value == 3 ? 0 : 1; }
SRC
"$minic" -S "$work/pragma.c" -o "$work/pragma.s"

cat >"$work/weak.c" <<'SRC'
extern const void weak_marker __attribute__((__weak__));
extern const unsigned long weak_table[] __attribute__((weak));
int probe(void) { return &weak_marker != (const void *)0 && weak_table != (const unsigned long *)0; }
SRC
"$minic" -S "$work/weak.c" -o "$work/weak.s"
grep -F '.weak weak_marker' "$work/weak.s" >/dev/null
grep -F '.weak weak_table' "$work/weak.s" >/dev/null

cat >"$work/tentative-array.c" <<'SRC'
struct op { int value; };
static const struct op first = { 1 };
static const struct op * const kind_ops[4];
static const struct op * const kind_ops[4] = {
    [1] = &first,
};
int main(void) { return kind_ops[1] == &first ? 0 : 1; }
SRC
"$minic" -S "$work/tentative-array.c" -o "$work/tentative-array.s"

grep -F '.size kind_ops, 32' "$work/tentative-array.s" >/dev/null

cat >"$work/range.c" <<'SRC'
static int anchor;
struct row { int value; int *pointer; };
static struct row rows[5] = {
    [1 ... 3] = { .value = 7, .pointer = &anchor },
};
static struct row inferred[] = {
    [2 ... 4] = { .value = 9, .pointer = &anchor },
};
int main(void)
{
    return rows[0].value == 0 && rows[1].value == 7 && rows[2].value == 7 &&
                   rows[3].value == 7 && rows[4].value == 0 && inferred[2].value == 9 &&
                   inferred[4].value == 9
               ? 0
               : 1;
}
SRC
"$minic" -S "$work/range.c" -o "$work/range.s"
grep -F '.size rows, 80' "$work/range.s" >/dev/null
grep -F '.size inferred, 80' "$work/range.s" >/dev/null
test "$(grep -c '  .word 7' "$work/range.s")" -eq 3
test "$(grep -c '  .word 9' "$work/range.s")" -eq 3

printf '%s\n' 'PASS compiler/c0/linux-tail-batch4 pragma-gcc-diagnostic=parse-only weak-extern=1 static-tentative-array=definition range-aggregate=evaluate-once+relocation-clone'
