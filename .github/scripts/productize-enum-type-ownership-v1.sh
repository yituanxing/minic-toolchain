#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
log=/tmp/minic-enum-type-ownership.log
patch=/tmp/minic-enum-type-ownership.patch

run_materialization() {
  python3 tools/dev/materialize-enum-type-ownership-v1.py

  # Repair two deliberately broad source substitutions in the materializer into strict C11.
  python3 - <<'PY'
from pathlib import Path

# The broad signedness substitution also touches the helper body; restore its primitive leaf.
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

# Replace the temporary GNU statement-expression spelling with a normal helper.
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
  if test -n "$changed_c"; then
    # shellcheck disable=SC2086
    clang-format-18 -i $changed_c
  fi
  CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh check
  git diff --check

  make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/product-enum-type
  MINIC="$root/build/product-enum-type/bin/minic" \
    HOST_CC="${CC:-cc}" \
    BUILD_DIR="$root/build/product-enum-type" \
    sh tests/compiler/c0/run-enum-forward-completion.sh
  make -j4 check-fast MODE=release BUILD_DIR=build/product-enum-type-fast

  # Structural contract: completing an enum must touch only its canonical entity.
  ! grep -R "minic_refresh_program_enum_types\|minic_refresh_enum_type" \
      src/frontend/ast.c src/frontend/*.h
  ! grep -R "minic_type_enum(enum_id," src/frontend/parser_enum.c src/frontend/type.c
}

if ! run_materialization > >(tee "$log") 2>&1; then
  git diff > "$patch" || true
  git reset --hard HEAD
  mkdir -p diagnostics
  cp "$log" diagnostics/enum-type-ownership-failure.log
  cp "$patch" diagnostics/enum-type-ownership-failure.patch
  git config user.name github-actions[bot]
  git config user.email 41898282+github-actions[bot]@users.noreply.github.com
  git add diagnostics/enum-type-ownership-failure.log diagnostics/enum-type-ownership-failure.patch
  git commit -m 'diagnostic: capture enum ownership productizer failure'
  git push origin HEAD:refactor/frontend-semantic-ownership
  exit 1
fi

git config user.name github-actions[bot]
git config user.email 41898282+github-actions[bot]@users.noreply.github.com
rm -f diagnostics/enum-type-ownership-failure.log diagnostics/enum-type-ownership-failure.patch

git add src/frontend src/target tests/compiler/c0
# Do not publish the development materializer as product source.
git reset tools/dev/materialize-enum-type-ownership-v1.py >/dev/null 2>&1 || true

git commit -m 'frontend: make enum completion canonical and query-driven'
git push origin HEAD:refactor/frontend-semantic-ownership
