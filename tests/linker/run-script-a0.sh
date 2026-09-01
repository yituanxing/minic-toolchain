#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cc=\${HOST_CC:-cc}
build=\${BUILD_DIR:-"$root/build/debug"}
out="$build/tests/linker-script-a0"

mkdir -p "$out"

"$cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
  -Wstrict-prototypes -Wmissing-prototypes -Werror \
  -I"$root/linker/src" \
  "$root/linker/src/linker_script_lex.c" \
  "$root/linker/src/linker_script_expr.c" \
  "$root/linker/src/linker_script_parse.c" \
  "$root/linker/src/linker_script_match.c" \
  "$root/tests/linker/script_parse_test.c" \
  -o "$out/script-parse-test"

"$out/script-parse-test" "$root/tests/linker/linux-subset.lds"
