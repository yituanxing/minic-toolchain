#!/usr/bin/env bash
set -Eeuo pipefail
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Werror \
  -Iassembler/src -Ielf/include \
  assembler/src/expression.c tests/assembler/probe-common-expr-syntax.c \
  -o "$work/expr-syntax"
"$work/expr-syntax"
