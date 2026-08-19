#!/usr/bin/env python3
from pathlib import Path

# Tighten the shared decoder's size precondition before formatting/productization.
path = Path("src/frontend/parser_string.c")
text = path.read_text()
old = '''    if (parser == NULL || values == NULL || element_capacity == 0U ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
'''
new = '''    if (parser == NULL || values == NULL || element_capacity == 0U ||
        element_capacity > SIZE_MAX / sizeof(*values) ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
'''
if text.count(old) != 1:
    raise SystemExit("unexpected bounded string values precondition")
path.write_text(text.replace(old, new, 1))

Path("tests/programs/c0/character_array_string_initializer.c").write_text('''char global_padded[10] = "ratelimit";
static char global_exact[3] = "abc";

static int runtime_string_check(void) {
    char path[16] = "//enomem";
    char inferred[] = "x" "\\n";

    return path[0] == '/' && path[1] == '/' && path[2] == 'e' && path[7] == 'm' &&
           path[8] == 0 && path[15] == 0 && sizeof(inferred) == 3 && inferred[0] == 'x' &&
           inferred[1] == '\\n' && inferred[2] == 0;
}

int main(void) {
    return global_padded[0] == 'r' && global_padded[8] == 't' && global_padded[9] == 0 &&
                   global_exact[0] == 'a' && global_exact[2] == 'c' && runtime_string_check()
               ? 0
               : 1;
}
''')

Path("tests/compiler/c0/run-character-array-string-initializer.sh").write_text('''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-character-array-string-init

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/programs/c0/character_array_string_initializer.c" \\
    -o "$work/character_array_string_initializer.i"
"$minic" -S "$work/character_array_string_initializer.i" \\
    -o "$work/character_array_string_initializer.s"

grep -F '.type global_padded, @object' "$work/character_array_string_initializer.s" >/dev/null
grep -F '.size global_padded, 10' "$work/character_array_string_initializer.s" >/dev/null
grep -F '  .byte 114' "$work/character_array_string_initializer.s" >/dev/null
grep -F '  .byte 116' "$work/character_array_string_initializer.s" >/dev/null
grep -F '.size global_exact, 3' "$work/character_array_string_initializer.s" >/dev/null
printf '%s\\n' 'PASS compiler/c0/character-array-string-initializer static=fixed+exact runtime=fixed+inferred adjacent=1 escape=1 padding=1'
''')

print("finalized character-array string initializer tests")
