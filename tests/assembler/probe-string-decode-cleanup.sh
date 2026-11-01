#!/usr/bin/env bash
# Direct ownership/error-path regression for the Common string decoder.
set -Eeuo pipefail
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=c11 -O2 -Wall -Wextra -Werror -Iassembler/src \
  tests/assembler/probe-string-decode-cleanup.c \
  assembler/src/string_literal.c -o "$work/string-decode"
"$work/string-decode"
