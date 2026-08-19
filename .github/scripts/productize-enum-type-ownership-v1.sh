#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
log=/tmp/minic-enum-type-ownership.log
patch=/tmp/minic-enum-type-ownership.patch

run_materialization() {
  python3 tools/dev/materialize-enum-type-ownership-v2.py

  # Repair two deliberately broad source substitutions in the inherited v1 stage-2 materializer
  # into strict, non-recursive C11 helpers.
  python3 - <<'PY'
from pathlib import Path

for name in ["src/frontend/const_eval.c", "src/frontend/expression_semantics.c"]:
    path = Path(name)
    text = path.read_text()
    text = text.replace(
        "minic_c0_type_effective_integer_type(program, type, &effective_type) &&\n"
        "           integer_type_is_signed(program, effective_type);",
        "minic_c0_type_effective_integer_type(program, type, &effective_type) &&\n"
        "           minic_type_is_signed_integer(effective_type);",
    )
    path.write_text(text)

path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text()
needle = "static bool minic_riscv64_emit_bit_field_load_from_address(FILE *file,"
helper = '''static bool minic_riscv64_integer_type_is_signed(const MinicC0Program *program,
                                                  MinicType type) {
    MinicType effective_type;

    return minic_c0_type_effective_integer_type(program, type, &effective_type) &&
           minic_type_is_signed_integer(effective_type);
}

'''
if helper not in text:
    if needle not in text:
        raise SystemExit("codegen_expression.c: bit-field helper anchor missing")
    text = text.replace(needle, helper + needle, 1)
old = '''({
                           MinicType effective_field_type;
                           minic_c0_type_effective_integer_type(
                               program, field->type, &effective_field_type) &&
                               minic_type_is_signed_integer(effective_field_type);
                       }) &&'''
if old not in text:
    raise SystemExit("codegen_expression.c: temporary signed bit-field expression missing")
text = text.replace(old, "minic_riscv64_integer_type_is_signed(program, field->type) &&", 1)
path.write_text(text)
PY

  changed_c=$(git diff --name-only -- '*.c' '*.h')
  test -n "$changed_c"
  # shellcheck disable=SC2086
  clang-format-18 -i $changed_c
  CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh check
  git diff --check

  for path in \
    src/frontend/ast.c \
    src/frontend/ast.h \
    src/frontend/ast_verifier.c \
    src/frontend/const_eval.c \
    src/frontend/expression_semantics.c \
    src/frontend/parser_expression.c \
    src/frontend/parser_statement.c \
    src/target/data_layout.c \
    src/target/target_info.c \
    src/target/target_info.h \
    src/target/riscv64/codegen_internal.h \
    src/target/riscv64/codegen_support.c \
    src/target/riscv64/codegen_expression.c \
    src/target/riscv64/codegen_statement.c \
    src/target/riscv64/core_codegen.c \
    tests/compiler/c0/enum_forward_completion.c \
    tests/compiler/c0/run-enum-forward-completion.sh \
    tests/compiler/c0/run.sh
  do
    git diff --name-only -- "$path" | grep -Fx "$path" >/dev/null
  done

  ! grep -R "minic_refresh_program_enum_types\|minic_refresh_enum_type" \
      src/frontend/ast.c src/frontend/*.h
  ! grep -R "minic_type_enum(enum_id," src/frontend/parser_enum.c src/frontend/type.c

  make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/product-enum-type
  MINIC="$root/build/product-enum-type/bin/minic" \
    HOST_CC="${CC:-cc}" \
    BUILD_DIR="$root/build/product-enum-type" \
    sh tests/compiler/c0/run-enum-forward-completion.sh
  make -j4 check-fast MODE=release BUILD_DIR=build/product-enum-type-fast
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
  cp "$log" diagnostics/enum-type-ownership-failure.log
  cp "$patch" diagnostics/enum-type-ownership-failure.patch
  git config user.name github-actions[bot]
  git config user.email 41898282+github-actions[bot]@users.noreply.github.com
  git add -f diagnostics/enum-type-ownership-failure.log diagnostics/enum-type-ownership-failure.patch
  git commit -m 'diagnostic: capture enum ownership productizer failure'
  git push origin HEAD:refactor/frontend-semantic-ownership
  exit "$status"
fi

git config user.name github-actions[bot]
git config user.email 41898282+github-actions[bot]@users.noreply.github.com
rm -f diagnostics/enum-type-ownership-failure.log diagnostics/enum-type-ownership-failure.patch

git add src/frontend src/target tests/compiler/c0
git reset tools/dev/materialize-enum-type-ownership-v1.py \
          tools/dev/materialize-enum-type-ownership-v2.py >/dev/null 2>&1 || true

git commit -m 'frontend: finish canonical enum type ownership'
git push origin HEAD:refactor/frontend-semantic-ownership
