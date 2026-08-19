#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    (ROOT / path).write_text(text)


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old!r}")
    write(path, text.replace(old, new, 1))


# C permits a typedef name to denote an incomplete array type. The AST already
# treats a TypeAlias as a semantic owner for an incomplete array descriptor, so
# this is a parser-routing fix rather than a new representation.
replace_once(
    "src/frontend/parser_typedef.c",
    """        if (!minic_parser_parse_array_declarator_suffix(
                parser, aliased_type, false, &aliased_type, &is_array) ||
""",
    """        if (!minic_parser_parse_array_declarator_suffix(
                parser, aliased_type, true, &aliased_type, &is_array) ||
""",
)

write(
    "tests/compiler/c0/incomplete_array_typedef.c",
    r'''struct match_token {
    int token;
};

typedef struct match_token match_table_t[];
typedef match_table_t *match_table_pointer_t;

int main(void)
{
    match_table_pointer_t table = 0;
    return table != 0;
}
''',
)

write(
    "tests/compiler/c0/incomplete_array_typedef_nested_bad.c",
    r'''typedef int bad_match_table_t[2][];
''',
)

write(
    "tests/compiler/c0/run-incomplete-array-typedef.sh",
    r'''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/incomplete-array-typedef
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/incomplete_array_typedef.c" -o "$work/good.i"
"$minic" -S "$work/good.i" -o "$work/good.s"
test -s "$work/good.s"

# The extension is intentionally only the outermost incomplete array owner.
# An array element itself may not be an incomplete array type.
set +e
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/incomplete_array_typedef_nested_bad.c" -o "$work/bad.i" 2>/dev/null
host_status=$?
set -e
if test "$host_status" -eq 0; then
  set +e
  "$minic" -S "$work/bad.i" -o "$work/bad.s" 2>"$work/bad.err"
  status=$?
  set -e
  test "$status" -ne 0
  grep -F 'only the outermost array dimension may be incomplete' "$work/bad.err" >/dev/null
fi

printf '%s\n' 'PASS compiler/c0/incomplete-array-typedef owner=typedef nested-incomplete=fail-closed'
''',
)

replace_once(
    "tests/compiler/c0/run.sh",
    """MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-first500-pareto-v1.sh"
""",
    """MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-first500-pareto-v1.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-incomplete-array-typedef.sh"
""",
)

print("INCOMPLETE_ARRAY_TYPEDEF_V1_MATERIALIZED")
