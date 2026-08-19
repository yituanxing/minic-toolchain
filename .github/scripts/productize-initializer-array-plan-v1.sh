#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
log=/tmp/minic-initializer-array-plan.log
patch=/tmp/minic-initializer-array-plan.patch

run_materialization() {
  python3 tools/dev/materialize-initializer-array-plan-v1.py

  # Python regex replacement strings interpret backslash escapes. The static-array replacement
  # currently contains one C '\0' literal, so normalize any accidental NUL byte immediately after
  # the grouped materialization and fail closed if any control-byte corruption survives.
  python3 - <<'PY'
from pathlib import Path

for name in ["src/frontend/parser_global.c", "src/frontend/parser_statement.c"]:
    path = Path(name)
    data = path.read_bytes()
    if b"\x00" in data:
        data = data.replace(b"\x00", b"\\0")
        path.write_bytes(data)

for path in Path("src").rglob("*.[ch]"):
    if b"\x00" in path.read_bytes():
        raise SystemExit(f"NUL byte survived initializer materialization: {path}")
PY

  # `initializer.c/.h` are already tracked on this stacked branch before the materializer runs,
  # so they do not necessarily appear in `git diff`. Format the semantic owner explicitly along
  # with every tracked/untracked C/H path produced by the grouped replacement.
  changed_c=$(
    {
      printf '%s\n' src/frontend/initializer.c src/frontend/initializer.h
      git diff --name-only -- '*.c' '*.h'
      git ls-files --others --exclude-standard -- '*.c' '*.h'
    } | sort -u
  )
  test -n "$changed_c"
  # shellcheck disable=SC2086
  clang-format-18 -i $changed_c
  CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh check
  git diff --check

  # The first initializer slice is intentionally a complete array-semantics unit: the semantic
  # owner must be compiled and consumed by both static and runtime scalar initializer paths.
  grep -F 'src/frontend/initializer.c' Makefile >/dev/null
  grep -F '#include "frontend/initializer.h"' src/frontend/parser_global.c >/dev/null
  grep -F '#include "frontend/initializer.h"' src/frontend/parser_statement.c >/dev/null
  grep -F 'minic_array_initializer_plan_add_designated' src/frontend/parser_global.c >/dev/null
  grep -F 'minic_array_initializer_plan_add_designated' src/frontend/parser_statement.c >/dev/null
  grep -F 'add_runtime_initializer_once_read' src/frontend/parser_statement.c >/dev/null
  grep -F 'return parse_fixed_runtime_scalar_array_initializer(' src/frontend/parser_statement.c >/dev/null

  make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/product-init-array
  MINIC="$root/build/product-init-array/bin/minic" \
    HOST_CC="${CC:-cc}" \
    BUILD_DIR="$root/build/product-init-array" \
    sh tests/compiler/c0/run-gnu-array-range-initializer.sh
  make -j4 check-fast MODE=release BUILD_DIR=build/product-init-array-fast
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
  cp "$log" diagnostics/initializer-array-plan-failure.log
  cp "$patch" diagnostics/initializer-array-plan-failure.patch
  git config user.name github-actions[bot]
  git config user.email 41898282+github-actions[bot]@users.noreply.github.com
  git add -f diagnostics/initializer-array-plan-failure.log \
             diagnostics/initializer-array-plan-failure.patch
  git commit -m 'diagnostic: capture initializer array-plan failure'
  git push origin HEAD:refactor/initializer-semantic-plan-v1
  exit "$status"
fi

git config user.name github-actions[bot]
git config user.email 41898282+github-actions[bot]@users.noreply.github.com
if git ls-files --error-unmatch diagnostics/initializer-array-plan-failure.log >/dev/null 2>&1; then
  git rm -f diagnostics/initializer-array-plan-failure.log
fi
if git ls-files --error-unmatch diagnostics/initializer-array-plan-failure.patch >/dev/null 2>&1; then
  git rm -f diagnostics/initializer-array-plan-failure.patch
fi

git add Makefile src/frontend tests/compiler/c0
git reset tools/dev/materialize-initializer-array-plan-v1.py >/dev/null 2>&1 || true
git commit -m 'frontend: unify scalar array initializer semantics'
git push origin HEAD:refactor/initializer-semantic-plan-v1
