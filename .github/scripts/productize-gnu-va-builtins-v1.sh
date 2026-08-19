#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
log=/tmp/minic-gnu-va-builtins-v1.log
patch=/tmp/minic-gnu-va-builtins-v1.patch

run_materialization() {
  python3 tools/dev/materialize-gnu-va-builtins-v1.py

  changed_c=$(
    {
      git diff --name-only -- '*.c' '*.h'
      git ls-files --others --exclude-standard -- '*.c' '*.h'
    } | sort -u
  )
  test -n "$changed_c"
  # shellcheck disable=SC2086
  clang-format-18 -i $changed_c
  chmod +x tests/compiler/c0/run-gnu-va-builtins.sh

  CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh check
  git diff --check

  # Architecture contracts: semantic builtin nodes are first class and the
  # existing internal RV64 helper path shares one va-start pointer owner.
  grep -F 'MINIC_EXPRESSION_BUILTIN_VA_START' src/frontend/ast.h >/dev/null
  grep -F 'MINIC_EXPRESSION_BUILTIN_VA_END' src/frontend/ast.h >/dev/null
  grep -F 'MINIC_EXPRESSION_BUILTIN_VA_START' src/frontend/ast_traversal.c >/dev/null
  grep -F 'second argument must be the last named parameter' src/frontend/parser_expression.c >/dev/null
  grep -F 'minic_riscv64_emit_va_start_pointer' src/target/riscv64/codegen_expression.c >/dev/null

  make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/product-gnu-va-builtins-v1
  MINIC="$root/build/product-gnu-va-builtins-v1/bin/minic" \
    HOST_CC="${CC:-cc}" \
    BUILD_DIR="$root/build/product-gnu-va-builtins-v1" \
    sh tests/compiler/c0/run-gnu-va-builtins.sh
  make -j4 check-fast MODE=release BUILD_DIR=build/product-gnu-va-builtins-v1-fast
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
  cp "$log" diagnostics/gnu-va-builtins-v1-failure.log
  cp "$patch" diagnostics/gnu-va-builtins-v1-failure.patch
  git config user.name github-actions[bot]
  git config user.email 41898282+github-actions[bot]@users.noreply.github.com
  git add -f diagnostics/gnu-va-builtins-v1-failure.log diagnostics/gnu-va-builtins-v1-failure.patch
  git commit -m 'diagnostic: capture GNU va builtin v1 failure'
  git push origin HEAD:frontend/gnu-va-start-builtin-v1
  exit "$status"
fi

git config user.name github-actions[bot]
git config user.email 41898282+github-actions[bot]@users.noreply.github.com
if git ls-files --error-unmatch diagnostics/gnu-va-builtins-v1-failure.log >/dev/null 2>&1; then
  git rm -f diagnostics/gnu-va-builtins-v1-failure.log
fi
if git ls-files --error-unmatch diagnostics/gnu-va-builtins-v1-failure.patch >/dev/null 2>&1; then
  git rm -f diagnostics/gnu-va-builtins-v1-failure.patch
fi

git add src/frontend/ast.h \
        src/frontend/ast_traversal.c \
        src/frontend/parser_expression.c \
        src/frontend/ast_verifier.c \
        src/target/riscv64/codegen_expression.c \
        tests/compiler/c0/gnu_builtin_va_start.c \
        tests/compiler/c0/gnu_builtin_va_start_wrong_last.c \
        tests/compiler/c0/run-gnu-va-builtins.sh \
        tests/compiler/c0/run.sh \
        tests/compiler/c0/run-runtime.sh

git commit -m 'frontend: add semantic GNU va_start and va_end builtins'
git push origin HEAD:frontend/gnu-va-start-builtin-v1
