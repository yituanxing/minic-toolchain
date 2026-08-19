#!/bin/sh
set -eu
clang-format -i src/frontend/parser_function.c src/frontend/parser_global.c
clang-format --dry-run --Werror src/frontend/parser_function.c src/frontend/parser_global.c
