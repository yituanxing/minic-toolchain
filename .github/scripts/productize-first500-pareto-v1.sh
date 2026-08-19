#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
log=/tmp/minic-first500-pareto-v1.log
patch=/tmp/minic-first500-pareto-v1.patch

run_materialization() {
  python3 tools/dev/materialize-first500-pareto-v1.py

  changed_c=$(
    {
      git diff --name-only -- '*.c' '*.h'
      git ls-files --others --exclude-standard -- '*.c' '*.h'
    } | sort -u
  )
  test -n "$changed_c"
  # shellcheck disable=SC2086
  clang-format-18 -i $changed_c
  chmod +x tests/compiler/c0/run-first500-pareto-v1.sh
  CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh check
  git diff --check

  # Structural ownership contracts for the two dominant first500 mechanisms.
  grep -F 'MINIC_ATTRIBUTE_NONSTRING' src/frontend/attribute.h >/dev/null
  grep -F 'MINIC_ATTRIBUTE_NONSTRING' src/frontend/parser_record.c >/dev/null
  grep -F 'minic_type_is_record(target_type)' src/frontend/parser_expression.c >/dev/null
  grep -F 'MINIC_EXPRESSION_LVALUE_READ' src/frontend/cast_normalization.c >/dev/null
  grep -F 'MINIC_EXPRESSION_LVALUE_READ' src/frontend/ast.c >/dev/null

  make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/product-first500-pareto-v1
  MINIC="$root/build/product-first500-pareto-v1/bin/minic" \
    HOST_CC="${CC:-cc}" \
    BUILD_DIR="$root/build/product-first500-pareto-v1" \
    sh tests/compiler/c0/run-first500-pareto-v1.sh
  make -j4 check-fast MODE=release BUILD_DIR=build/product-first500-pareto-v1-fast
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
  cp "$log" diagnostics/first500-pareto-v1-failure.log
  cp "$patch" diagnostics/first500-pareto-v1-failure.patch
  git config user.name github-actions[bot]
  git config user.email 41898282+github-actions[bot]@users.noreply.github.com
  git add -f diagnostics/first500-pareto-v1-failure.log diagnostics/first500-pareto-v1-failure.patch
  git commit -m 'diagnostic: capture first500 Pareto v1 failure'
  git push origin HEAD:refactor/linux-first500-capabilities-v1
  exit "$status"
fi

git config user.name github-actions[bot]
git config user.email 41898282+github-actions[bot]@users.noreply.github.com
if git ls-files --error-unmatch diagnostics/first500-pareto-v1-failure.log >/dev/null 2>&1; then
  git rm -f diagnostics/first500-pareto-v1-failure.log
fi
if git ls-files --error-unmatch diagnostics/first500-pareto-v1-failure.patch >/dev/null 2>&1; then
  git rm -f diagnostics/first500-pareto-v1-failure.patch
fi

git add src/frontend src/target/riscv64/codegen_expression.c tests/compiler/c0
git reset tools/dev/materialize-first500-pareto-v1.py >/dev/null 2>&1 || true
git commit -m 'frontend: cover dominant first500 cast and field-attribute mechanisms'
git push origin HEAD:refactor/linux-first500-capabilities-v1
