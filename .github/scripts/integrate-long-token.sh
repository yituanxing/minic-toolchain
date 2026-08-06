#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"

python3 - <<'PY'
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}")
    p.write_text(text.replace(old, new, 1))

replace_once(
    "src/frontend/token.h",
    "    MINIC_TOKEN_KW_CHAR,\n    MINIC_TOKEN_KW_INT,\n    MINIC_TOKEN_KW_UNSIGNED,",
    "    MINIC_TOKEN_KW_CHAR,\n    MINIC_TOKEN_KW_INT,\n    MINIC_TOKEN_KW_LONG,\n    MINIC_TOKEN_KW_UNSIGNED,",
)

replace_once(
    "src/frontend/token.c",
    '    case MINIC_TOKEN_KW_INT:\n        return "int";\n    case MINIC_TOKEN_KW_UNSIGNED:',
    '    case MINIC_TOKEN_KW_INT:\n        return "int";\n    case MINIC_TOKEN_KW_LONG:\n        return "long";\n    case MINIC_TOKEN_KW_UNSIGNED:',
)

replace_once(
    "src/frontend/lexer.c",
    '    if (length == 3U && memcmp(text, "int", 3U) == 0) {\n        return MINIC_TOKEN_KW_INT;\n    }\n    if (length == 8U && memcmp(text, "unsigned", 8U) == 0) {',
    '    if (length == 3U && memcmp(text, "int", 3U) == 0) {\n        return MINIC_TOKEN_KW_INT;\n    }\n    if (length == 4U && memcmp(text, "long", 4U) == 0) {\n        return MINIC_TOKEN_KW_LONG;\n    }\n    if (length == 8U && memcmp(text, "unsigned", 8U) == 0) {',
)

replace_once(
    "tests/frontend/token_model_test.c",
    '        expect_name(MINIC_TOKEN_KW_CHAR, "char") != 0 ||\n        expect_name(MINIC_TOKEN_KW_STRUCT, "struct") != 0 ||',
    '        expect_name(MINIC_TOKEN_KW_CHAR, "char") != 0 ||\n        expect_name(MINIC_TOKEN_KW_LONG, "long") != 0 ||\n        expect_name(MINIC_TOKEN_KW_STRUCT, "struct") != 0 ||',
)

replace_once(
    "tests/frontend/lexer_test.c",
    "static int test_char_keyword_boundaries(void)\n{",
    '''static int test_long_keyword_boundaries(void)
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

static int test_char_keyword_boundaries(void)
{''',
)

replace_once(
    "tests/frontend/lexer_test.c",
    "        test_unsigned_keyword_boundaries() != 0 ||\n        test_char_keyword_boundaries() != 0 ||",
    "        test_unsigned_keyword_boundaries() != 0 ||\n        test_long_keyword_boundaries() != 0 ||\n        test_char_keyword_boundaries() != 0 ||",
)
PY

CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh write
make -j2 check-token-model check-lexer

git rm -- .github/scripts/integrate-long-token.sh .github/workflows/integrate-long-token.yml

git config user.name github-actions[bot]
git config user.email 41898282+github-actions[bot]@users.noreply.github.com
git add src/frontend/token.h src/frontend/token.c src/frontend/lexer.c \
    tests/frontend/token_model_test.c tests/frontend/lexer_test.c
git commit -m "frontend: add the long keyword token

Introduce a distinct long keyword identity and Lexer classification while preserving identifier boundaries such as longer and long_value.

Add permanent token-name and Lexer boundary coverage. This commit does not yet claim long integer type or RV64 width semantics.

中文说明：
增加独立 long 关键字身份与 Lexer 分类，并保持 longer、long_value 等标识符边界。

加入永久 Token 名称和 Lexer 边界覆盖；本提交暂不宣称已经实现 long 整数类型或 RV64 宽度语义。"
git push origin HEAD:frontend/rv64-long-integers
