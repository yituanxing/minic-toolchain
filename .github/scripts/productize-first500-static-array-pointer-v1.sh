#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
log=/tmp/minic-first500-static-array-pointer-v1.log
patch=/tmp/minic-first500-static-array-pointer-v1.patch

run_materialization() {
  python3 tools/dev/materialize-first500-static-array-pointer-v1.py

  changed_c=$(
    {
      git diff --name-only -- '*.c' '*.h'
      git ls-files --others --exclude-standard -- '*.c' '*.h'
    } | sort -u
  )
  test -n "$changed_c"
  # shellcheck disable=SC2086
  clang-format-18 -i $changed_c
  chmod +x tests/compiler/c0/run-first500-static-array-pointer-v1.sh
  CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh check
  git diff --check

  # One owner for static pointer relocation, one owner for scalar-array plan.
  grep -F 'minic_parser_parse_static_pointer_object_initializer' \
    src/frontend/parser_statement.c >/dev/null
  grep -F 'parse_static_scalar_array_transaction(parser, object_id, element_type, 0U, true)' \
    src/frontend/parser_global.c >/dev/null

  make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/product-first500-static-array-pointer-v1
  MINIC="$root/build/product-first500-static-array-pointer-v1/bin/minic" \
    HOST_CC="${CC:-cc}" \
    BUILD_DIR="$root/build/product-first500-static-array-pointer-v1" \
    sh tests/compiler/c0/run-first500-static-array-pointer-v1.sh
  make -j4 check-fast MODE=release BUILD_DIR=build/product-first500-static-array-pointer-v1-fast
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
  cp "$log" diagnostics/first500-static-array-pointer-v1-failure.log
  cp "$patch" diagnostics/first500-static-array-pointer-v1-failure.patch
  git config user.name github-actions[bot]
  git config user.email 41898282+github-actions[bot]@users.noreply.github.com
  git add -f diagnostics/first500-static-array-pointer-v1-failure.log \
             diagnostics/first500-static-array-pointer-v1-failure.patch
  git commit -m 'diagnostic: capture first500 static array pointer v1 failure'
  git push origin HEAD:frontend/first500-static-array-pointer-v1
  exit "$status"
fi

git config user.name github-actions[bot]
git config user.email 41898282+github-actions[bot]@users.noreply.github.com
if git ls-files --error-unmatch diagnostics/first500-static-array-pointer-v1-failure.log >/dev/null 2>&1; then
  git rm -f diagnostics/first500-static-array-pointer-v1-failure.log
fi
if git ls-files --error-unmatch diagnostics/first500-static-array-pointer-v1-failure.patch >/dev/null 2>&1; then
  git rm -f diagnostics/first500-static-array-pointer-v1-failure.patch
fi

git add src/frontend/parser_statement.c \
        src/frontend/parser_global.c \
        tests/compiler/c0/static_local_pointer_array_decay.c \
        tests/compiler/c0/inferred_static_unsigned_char_list.c \
        tests/compiler/c0/run-first500-static-array-pointer-v1.sh \
        tests/compiler/c0/run.sh \
        tests/compiler/c0/run-runtime.sh

git commit -m 'frontend: reuse static relocation and scalar-array owners'
git push origin HEAD:frontend/first500-static-array-pointer-v1
