#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
log=/tmp/minic-incomplete-array-typedef-v1.log
patch=/tmp/minic-incomplete-array-typedef-v1.patch

run_materialization() {
  python3 tools/dev/materialize-incomplete-array-typedef-v1.py

  changed_c=$(
    {
      git diff --name-only -- '*.c' '*.h'
      git ls-files --others --exclude-standard -- '*.c' '*.h'
    } | sort -u
  )
  test -n "$changed_c"
  # shellcheck disable=SC2086
  clang-format-18 -i $changed_c
  chmod +x tests/compiler/c0/run-incomplete-array-typedef.sh

  CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh check
  git diff --check

  grep -F 'parser, aliased_type, true, &aliased_type, &is_array' \
    src/frontend/parser_typedef.c >/dev/null
  grep -F 'program->type_aliases[index].type' src/frontend/ast_verifier.c >/dev/null

  make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/product-incomplete-array-typedef-v1
  MINIC="$root/build/product-incomplete-array-typedef-v1/bin/minic" \
    HOST_CC="${CC:-cc}" \
    BUILD_DIR="$root/build/product-incomplete-array-typedef-v1" \
    sh tests/compiler/c0/run-incomplete-array-typedef.sh
  make -j4 check-fast MODE=release BUILD_DIR=build/product-incomplete-array-typedef-v1-fast
}

set +e
(
  set -Eeuo pipefail
  run_materialization
) > >(tee "$log") 2>&1
status=$?
set -e

if test "$status" -ne 0; then
  git diff > "$patch" || true
  git reset --hard HEAD
  mkdir -p diagnostics
  cp "$log" diagnostics/incomplete-array-typedef-v1-failure.log
  cp "$patch" diagnostics/incomplete-array-typedef-v1-failure.patch
  git config user.name github-actions[bot]
  git config user.email 41898282+github-actions[bot]@users.noreply.github.com
  git add -f diagnostics/incomplete-array-typedef-v1-failure.log \
             diagnostics/incomplete-array-typedef-v1-failure.patch
  git commit -m 'diagnostic: capture incomplete array typedef v1 failure'
  git push origin HEAD:frontend/incomplete-array-typedef-v1
  exit "$status"
fi

git config user.name github-actions[bot]
git config user.email 41898282+github-actions[bot]@users.noreply.github.com
if git ls-files --error-unmatch diagnostics/incomplete-array-typedef-v1-failure.log >/dev/null 2>&1; then
  git rm -f diagnostics/incomplete-array-typedef-v1-failure.log
fi
if git ls-files --error-unmatch diagnostics/incomplete-array-typedef-v1-failure.patch >/dev/null 2>&1; then
  git rm -f diagnostics/incomplete-array-typedef-v1-failure.patch
fi

git add src/frontend/parser_typedef.c \
        tests/compiler/c0/incomplete_array_typedef.c \
        tests/compiler/c0/incomplete_array_typedef_nested_bad.c \
        tests/compiler/c0/run-incomplete-array-typedef.sh \
        tests/compiler/c0/run.sh

git commit -m 'frontend: support incomplete array typedef owners'
git push origin HEAD:frontend/incomplete-array-typedef-v1
