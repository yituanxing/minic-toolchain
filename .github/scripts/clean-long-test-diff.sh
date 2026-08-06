#!/usr/bin/env bash
set -Eeuo pipefail

git fetch origin main
git checkout origin/main -- \
  tests/frontend/token_model_test.c \
  tests/frontend/lexer_test.c \
  tests/frontend/type_test.c \
  tests/target/riscv64/layout_test.c

python3 - <<'PY'
from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"{path}: marker count={text.count(old)}")
    p.write_text(text.replace(old, new, 1))

replace_once(
    'tests/frontend/token_model_test.c',
    '        expect_name(MINIC_TOKEN_KW_CHAR, "char") != 0 ||\n',
    '        expect_name(MINIC_TOKEN_KW_CHAR, "char") != 0 ||\n'
    '        expect_name(MINIC_TOKEN_KW_LONG, "long") != 0 ||\n'
    '        expect_name(MINIC_TOKEN_KW_SIGNED, "signed") != 0 ||\n',
)

lexer = Path('tests/frontend/lexer_test.c')
text = lexer.read_text()
marker = 'static int test_char_keyword_boundaries(void)\n'
functions = '''static int test_signed_keyword_boundaries(void)
{
    static const char source[] = "signed signed_value signedness";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "signed.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_KW_SIGNED, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 8U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 21U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 31U) != 0) {
        return 1;
    }
    return 0;
}

static int test_long_keyword_boundaries(void)
{
    static const char source[] = "long longer long_value";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "long.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_KW_LONG, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 6U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 13U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 23U) != 0) {
        return 1;
    }
    return 0;
}

'''
if text.count(marker) != 1:
    raise SystemExit('lexer function marker mismatch')
text = text.replace(marker, functions + marker, 1)
old = '        test_unsigned_keyword_boundaries() != 0 ||\n        test_char_keyword_boundaries() != 0 ||'
new = ('        test_unsigned_keyword_boundaries() != 0 ||\n'
       '        test_signed_keyword_boundaries() != 0 ||\n'
       '        test_long_keyword_boundaries() != 0 ||\n'
       '        test_char_keyword_boundaries() != 0 ||')
if text.count(old) != 1:
    raise SystemExit('lexer call marker mismatch')
lexer.write_text(text.replace(old, new, 1))

replace_once(
    'tests/frontend/type_test.c',
    '    MinicType unsigned_integer_type;\n    MinicType unsigned_pointer_type;\n',
    '    MinicType unsigned_integer_type;\n'
    '    MinicType signed_long_type;\n'
    '    MinicType unsigned_long_type;\n'
    '    MinicType unsigned_pointer_type;\n',
)

type_test = Path('tests/frontend/type_test.c')
text = type_test.read_text()
identity_marker = '    unsigned_char_type = minic_type_unsigned_char();\n'
identity_block = '''    signed_long_type = minic_type_long();
    unsigned_long_type = minic_type_unsigned_long();
    if (!minic_type_is_integer(signed_long_type) ||
        !minic_type_is_long_integer(signed_long_type) ||
        !minic_type_is_signed_integer(signed_long_type) ||
        minic_type_is_unsigned_integer(signed_long_type) ||
        !minic_type_is_integer(unsigned_long_type) ||
        !minic_type_is_long_integer(unsigned_long_type) ||
        minic_type_is_signed_integer(unsigned_long_type) ||
        !minic_type_is_unsigned_integer(unsigned_long_type) ||
        minic_type_equal(signed_long_type, integer_type) ||
        minic_type_equal(unsigned_long_type, unsigned_integer_type) ||
        minic_type_equal(signed_long_type, unsigned_long_type)) {
        return fail("long integer identity");
    }

'''
if text.count(identity_marker) != 1:
    raise SystemExit('type identity marker mismatch')
text = text.replace(identity_marker, identity_block + identity_marker, 1)

promotion_old = '''        !minic_type_integer_promotion(unsigned_integer_type, &promoted_type) ||
        !minic_type_equal(promoted_type, unsigned_integer_type) ||
        minic_type_integer_promotion(void_type, &promoted_type) ||'''
promotion_new = '''        !minic_type_integer_promotion(unsigned_integer_type, &promoted_type) ||
        !minic_type_equal(promoted_type, unsigned_integer_type) ||
        !minic_type_integer_promotion(signed_long_type, &promoted_type) ||
        !minic_type_equal(promoted_type, signed_long_type) ||
        !minic_type_integer_promotion(unsigned_long_type, &promoted_type) ||
        !minic_type_equal(promoted_type, unsigned_long_type) ||
        minic_type_integer_promotion(void_type, &promoted_type) ||'''
if text.count(promotion_old) != 1:
    raise SystemExit('type promotion marker mismatch')
text = text.replace(promotion_old, promotion_new, 1)

common_old = '''        !minic_type_integer_common(
            unsigned_integer_type,
            integer_type,
            &common_type) ||
        !minic_type_equal(common_type, unsigned_integer_type) ||
        minic_type_integer_common(integer_type, void_type, &common_type) ||'''
common_new = '''        !minic_type_integer_common(
            unsigned_integer_type,
            integer_type,
            &common_type) ||
        !minic_type_equal(common_type, unsigned_integer_type) ||
        !minic_type_integer_common(
            integer_type,
            signed_long_type,
            &common_type) ||
        !minic_type_equal(common_type, signed_long_type) ||
        !minic_type_integer_common(
            unsigned_integer_type,
            signed_long_type,
            &common_type) ||
        !minic_type_equal(common_type, signed_long_type) ||
        !minic_type_integer_common(
            integer_type,
            unsigned_long_type,
            &common_type) ||
        !minic_type_equal(common_type, unsigned_long_type) ||
        !minic_type_integer_common(
            signed_long_type,
            unsigned_long_type,
            &common_type) ||
        !minic_type_equal(common_type, unsigned_long_type) ||
        minic_type_integer_common(integer_type, void_type, &common_type) ||'''
if text.count(common_old) != 1:
    raise SystemExit('type common marker mismatch')
type_test.write_text(text.replace(common_old, common_new, 1))

replace_once(
    'tests/target/riscv64/layout_test.c',
    '    size_t byte_size;\n    size_t byte_alignment;\n',
    '    size_t byte_size;\n'
    '    size_t byte_alignment;\n'
    '    size_t long_size;\n'
    '    size_t long_alignment;\n',
)

layout = Path('tests/target/riscv64/layout_test.c')
text = layout.read_text()
marker = '    if (!minic_c0_program_add_record(\n'
block = '''    if (!minic_riscv64_type_layout(
            &program,
            minic_type_long(),
            &long_size,
            &long_alignment) ||
        long_size != 8U || long_alignment != 8U ||
        !minic_riscv64_type_layout(
            &program,
            minic_type_unsigned_long(),
            &long_size,
            &long_alignment) ||
        long_size != 8U || long_alignment != 8U) {
        minic_c0_program_destroy(&program);
        return fail("RV64 long scalar layout");
    }

'''
if text.count(marker) != 1:
    raise SystemExit('layout marker mismatch')
layout.write_text(text.replace(marker, block + marker, 1))

Path('tests/compiler/c0/long_type_specifiers.c').write_text('''typedef long signed int signed_long_a;
typedef signed long signed_long_b;
typedef unsigned long int unsigned_long_a;
typedef long unsigned int size_word;

static signed_long_a identity_signed(signed_long_b value)
{
    return value;
}

static unsigned_long_a identity_unsigned(size_word value)
{
    return value;
}

int main(void)
{
    signed_long_a left = 3;
    size_word right = 4;
    return (int)(identity_signed(left) + identity_unsigned(right));
}
''')
Path('tests/compiler/c0/invalid_long_long.c').write_text('''int main(void)
{
    long long value = 0;
    return (int)value;
}
''')
Path('tests/compiler/c0/invalid_signed_unsigned.c').write_text('''int main(void)
{
    signed unsigned int value = 0;
    return value;
}
''')
Path('tests/compiler/c0/invalid_long_char.c').write_text('''int main(void)
{
    unsigned long char value = 0;
    return value;
}
''')
PY

make -j2 \
  check-token-model check-lexer check-type check-layout check-long-types
make -j2 check-fast

git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
git rm \
  .github/scripts/clean-long-test-diff.sh \
  .github/workflows/clean-long-test-diff.yml
git add tests
git commit -m "tests: keep long coverage diff minimal" -m "Restore the existing test files to their main-branch formatting and reapply only the focused LONG token, type-conversion, and RV64 layout assertions. Keep new fixtures consistent with the established test style.\n\n中文说明：恢复既有测试文件的主线排版，只重新加入 LONG Token、类型转换和 RV64 布局的聚焦断言；新用例保持现有测试风格。\n\nValidation / 验证： focused long gates and complete host fast gate PASS."
git push origin HEAD:frontend/rv64-long-integers
