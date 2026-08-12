from pathlib import Path
import subprocess

base = "76d3fdeb60c3f69bf1170d03e1e063b90f927c21"
path = "tests/frontend/lexer_test.c"
text = subprocess.check_output(["git", "show", f"{base}:{path}"], text=True)
needle = "static int test_invalid_string_literals(void)\n"
addition = r'''static int test_wide_string_literals(void)
{
    static const char source[] = "L\"SecureBoot\" Lvalue";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "wide-strings.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_WIDE_STRING_LITERAL, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 15U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 21U) != 0) {
        return 1;
    }
    return 0;
}

static int test_invalid_string_literals(void)
'''
if text.count(needle) != 1:
    raise SystemExit(f"lexer insertion anchor mismatch: {text.count(needle)}")
text = text.replace(needle, addition, 1)
old_main = '''        test_floating_constants() != 0 ||
        test_string_literals() != 0 ||
        test_invalid_string_literals() != 0 ||
'''
new_main = '''        test_floating_constants() != 0 ||
        test_string_literals() != 0 ||
        test_wide_string_literals() != 0 ||
        test_invalid_string_literals() != 0 ||
'''
if text.count(old_main) != 1:
    raise SystemExit(f"lexer main anchor mismatch: {text.count(old_main)}")
Path(path).write_text(text.replace(old_main, new_main, 1))
