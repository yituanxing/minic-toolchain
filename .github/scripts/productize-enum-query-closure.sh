#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"

python3 tools/dev/materialize-enum-query-closure.py

changed_c=$(git diff --name-only -- '*.c' '*.h')
test -n "$changed_c"
# shellcheck disable=SC2086
clang-format-18 -i $changed_c
CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh check
git diff --check

# Frontend semantic consumers must not bypass the canonical enum-aware target queries anymore.
if grep -R -n 'minic_target_info_integer_common(' src/frontend; then
  echo 'legacy frontend integer-common consumer survived enum closure' >&2
  exit 1
fi
if grep -R -n 'minic_target_info_integer_promotion(' src/frontend; then
  echo 'legacy frontend integer-promotion consumer survived enum closure' >&2
  exit 1
fi

make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/enum-query-closure
MINIC="$root/build/enum-query-closure/bin/minic" \
  HOST_CC="${CC:-cc}" \
  BUILD_DIR="$root/build/enum-query-closure" \
  sh tests/compiler/c0/run-for-declaration-initializers.sh

# Freeze the standalone Core emitter contract: plain Core integer types need no Semantic AST owner.
mkdir -p build/enum-query-core
cc -std=c11 \
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
  -Wstrict-prototypes -Wmissing-prototypes -Werror \
  -Iinclude -Isrc \
  $(find src -name '*.c' -print | sort) \
  tests/target/riscv64/core_basic_emitter_test.c \
  -o build/enum-query-core/core-basic-emitter-test
build/enum-query-core/core-basic-emitter-test build/enum-query-core/core-basic-v0.s
test -s build/enum-query-core/core-basic-v0.s

git config user.name github-actions[bot]
git config user.email 41898282+github-actions[bot]@users.noreply.github.com
git add src/frontend/ast_verifier.c src/target/riscv64/codegen_support.c
git commit -m 'frontend: close enum-aware semantic query gaps'
git push origin HEAD:refactor/frontend-semantic-ownership
