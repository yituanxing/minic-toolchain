#!/usr/bin/env bash
set -Eeuo pipefail

python3 - <<'PY'
from pathlib import Path


def replace_once(path, old, new):
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one marker, found {count}")
    file.write_text(text.replace(old, new, 1))

replace_once(
    "src/frontend/type.c",
    '''    if (type.integer_rank == MINIC_INTEGER_RANK_INT ||
        type.integer_rank == MINIC_INTEGER_RANK_LONG) {
        *result = type;
        return true;
    }
''',
    '''    if (type.integer_rank == MINIC_INTEGER_RANK_INT) {
        *result =
            minic_type_is_unsigned_integer(type) ? minic_type_unsigned_int() : minic_type_int();
        return true;
    }
    if (type.integer_rank == MINIC_INTEGER_RANK_LONG) {
        *result = minic_type_is_unsigned_integer(type) ? minic_type_unsigned_long()
                                                      : minic_type_long();
        return true;
    }
''',
)

replace_once(
    "tests/frontend/type_test.c",
    '''    MinicType signed_long_type;
    MinicType unsigned_long_type;
    MinicType unsigned_pointer_type;
''',
    '''    MinicType signed_long_type;
    MinicType unsigned_long_type;
    MinicType const_long_type;
    MinicType unsigned_pointer_type;
''',
)

replace_once(
    "tests/frontend/type_test.c",
    '''    unsigned_char_type = minic_type_unsigned_char();
''',
    '''    if (!minic_type_add_const(signed_long_type, &const_long_type) ||
        !minic_type_is_const(const_long_type) ||
        !minic_type_is_long_integer(const_long_type)) {
        return fail("const long identity");
    }

    unsigned_char_type = minic_type_unsigned_char();
''',
)

replace_once(
    "tests/frontend/type_test.c",
    '''        !minic_type_integer_promotion(signed_long_type, &promoted_type) ||
        !minic_type_equal(promoted_type, signed_long_type) ||
        !minic_type_integer_promotion(unsigned_long_type, &promoted_type) ||
''',
    '''        !minic_type_integer_promotion(signed_long_type, &promoted_type) ||
        !minic_type_equal(promoted_type, signed_long_type) ||
        !minic_type_integer_promotion(const_long_type, &promoted_type) ||
        !minic_type_equal(promoted_type, signed_long_type) ||
        minic_type_is_const(promoted_type) ||
        !minic_type_integer_promotion(unsigned_long_type, &promoted_type) ||
''',
)
PY

clang-format-18 -i src/frontend/type.c
make -j2 check-type
bash .github/scripts/compiler-c0-full-gate.sh

git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
git rm \
  .github/scripts/fix-qualified-long-promotion.sh \
  .github/workflows/fix-qualified-long-promotion.yml
git add src/frontend/type.c tests/frontend/type_test.c
git commit -m "frontend: strip qualifiers during integer promotion" -m "Restore value-type semantics for integer promotion by rebuilding both INT and LONG results without base qualifiers. Add focused const-long coverage so qualified lvalues cannot leak const into promoted expression types.\n\n中文说明：整数提升时重新构造 INT 与 LONG 值类型，去除基础限定符，恢复正确的值类型语义；加入 const long 聚焦覆盖，防止限定左值把 const 泄漏到提升后的表达式类型。\n\nValidation / 验证： frontend type gate and complete clean-checkout compiler gate PASS."
git push origin HEAD:frontend/rv64-long-integers
