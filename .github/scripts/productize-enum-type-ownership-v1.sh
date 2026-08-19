#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
log=/tmp/minic-enum-type-ownership.log
patch=/tmp/minic-enum-type-ownership.patch

changed_path() {
  local path=$1
  git diff --name-only -- "$path" | grep -Fx "$path" >/dev/null ||
    git ls-files --others --exclude-standard -- "$path" | grep -Fx "$path" >/dev/null
}

run_materialization() {
  python3 tools/dev/materialize-enum-type-ownership-v2.py

  # Repair deliberately broad source substitutions in the inherited v1 stage-2 materializer into
  # strict C11, and thread `program` through the complete RV64 expression-helper family in one pass.
  python3 - <<'PY'
from pathlib import Path
import re

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

# Mixed floating/integer helpers now need canonical enum queries while converting integer operands.
# Thread the existing program context through the helper definitions and every call site together.
text, count = re.subn(
    r"static bool minic_riscv64_emit_double_binary\(FILE \*file,\n",
    "static bool minic_riscv64_emit_double_binary(FILE *file,\n"
    "                                             const MinicC0Program *program,\n",
    text,
    count=1,
)
if count != 1:
    raise SystemExit("codegen_expression.c: double-binary signature not found")
text, count = re.subn(
    r"static bool minic_riscv64_emit_double_comparison\(FILE \*file,\n",
    "static bool minic_riscv64_emit_double_comparison(FILE *file,\n"
    "                                                 const MinicC0Program *program,\n",
    text,
    count=1,
)
if count != 1:
    raise SystemExit("codegen_expression.c: double-comparison signature not found")

# Multi-line calls are common in the emitter. Definitions cannot match because they spell
# `FILE *file` on the same line as `(`.
def add_program_to_calls(source, function_name, minimum=1):
    pattern = rf"({re.escape(function_name)}\(\n\s*)file,"
    result, found = re.subn(pattern, r"\1file, program,", source)
    if found < minimum:
        raise SystemExit(f"codegen_expression.c: no calls updated for {function_name}")
    return result, found

for function_name, minimum in [
    ("minic_riscv64_emit_double_binary", 1),
    ("minic_riscv64_emit_double_comparison", 1),
    ("minic_riscv64_emit_bit_field_load_from_address", 1),
    ("minic_riscv64_emit_integer_result_conversion", 5),
    ("minic_riscv64_emit_conditional_result_conversion", 3),
]:
    text, _ = add_program_to_calls(text, function_name, minimum)

# Compound assignment keeps one compact same-line floating call. Close that call in the same
# grouped replacement rather than waiting for another compiler iteration.
compact_old = "minic_riscv64_emit_double_binary(file, operator_kind, target->type, value->type)"
compact_new = (
    "minic_riscv64_emit_double_binary(file, program, operator_kind, target->type, value->type)"
)
if text.count(compact_old) != 1:
    raise SystemExit(
        "codegen_expression.c: expected exactly one compact double-binary call without program"
    )
text = text.replace(compact_old, compact_new, 1)

path.write_text(text)
PY

  # Format both tracked modifications and newly-created C/H fixtures.
  changed_c=$(
    {
      git diff --name-only -- '*.c' '*.h'
      git ls-files --others --exclude-standard -- '*.c' '*.h'
    } | sort -u
  )
  test -n "$changed_c"
  # shellcheck disable=SC2086
  clang-format-18 -i $changed_c
  CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh check
  git diff --check

  # The grouped replacement is indivisible: every semantic consumer and focused fixture must be
  # present before we spend time compiling it. `changed_path` deliberately recognizes untracked
  # test fixtures as part of the slice.
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
    changed_path "$path"
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

# Canonical product commit replaces any previous failure diagnostic.
if git ls-files --error-unmatch diagnostics/enum-type-ownership-failure.log >/dev/null 2>&1; then
  git rm -f diagnostics/enum-type-ownership-failure.log
fi
if git ls-files --error-unmatch diagnostics/enum-type-ownership-failure.patch >/dev/null 2>&1; then
  git rm -f diagnostics/enum-type-ownership-failure.patch
fi

git add src/frontend src/target tests/compiler/c0
git reset tools/dev/materialize-enum-type-ownership-v1.py \
          tools/dev/materialize-enum-type-ownership-v2.py >/dev/null 2>&1 || true

git commit -m 'frontend: finish canonical enum type ownership'
git push origin HEAD:refactor/frontend-semantic-ownership
