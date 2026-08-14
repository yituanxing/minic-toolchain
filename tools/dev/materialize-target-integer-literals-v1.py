#!/usr/bin/env python3
from pathlib import Path


def read(path):
    return Path(path).read_text()


def write(path, text):
    Path(path).write_text(text)


def replace_exact(path, old, new, count=1):
    text = read(path)
    actual = text.count(old)
    if actual != count:
        raise SystemExit(f"{path}: expected {count} occurrences, found {actual}: {old[:100]!r}")
    write(path, text.replace(old, new))


# TargetInfo owns the target-resolved semantic type of plain char.
replace_exact(
    "src/target/target_info.h",
    "bool minic_target_info_plain_char_sign(const MinicTargetInfo *target, MinicIntegerSign *sign);\n",
    "bool minic_target_info_plain_char_sign(const MinicTargetInfo *target, MinicIntegerSign *sign);\n"
    "bool minic_target_info_plain_char_type(const MinicTargetInfo *target, MinicType *type);\n",
)
replace_exact(
    "src/target/target_info.c",
    "bool minic_target_info_wide_character_type(const MinicTargetInfo *target, MinicType *type) {\n",
    "bool minic_target_info_plain_char_type(const MinicTargetInfo *target, MinicType *type) {\n"
    "    MinicIntegerSign sign;\n\n"
    "    if (type == NULL || !minic_target_info_plain_char_sign(target, &sign)) {\n"
    "        return false;\n"
    "    }\n"
    "    *type = minic_type_char();\n"
    "    type->integer_sign = sign;\n"
    "    return true;\n"
    "}\n\n"
    "bool minic_target_info_wide_character_type(const MinicTargetInfo *target, MinicType *type) {\n",
)

# MinicType preserves plain-char identity; target policy decides its actual sign.
replace_exact(
    "src/frontend/type.c",
    "    if (type.integer_sign != MINIC_INTEGER_SIGN_UNSIGNED) {\n"
    "        return false;\n"
    "    }\n"
    "    return type.integer_rank == MINIC_INTEGER_RANK_CHAR;\n",
    "    if (type.integer_sign != MINIC_INTEGER_SIGN_SIGNED &&\n"
    "        type.integer_sign != MINIC_INTEGER_SIGN_UNSIGNED) {\n"
    "        return false;\n"
    "    }\n"
    "    return type.integer_rank == MINIC_INTEGER_RANK_CHAR;\n",
)

# Unsuffixed `char` declarations use the active target model.
replace_exact(
    "src/frontend/parser_type.c",
    "            parsed_type = saw_signed     ? minic_type_signed_char()\n"
    "                          : saw_unsigned ? minic_type_unsigned_char()\n"
    "                                         : minic_type_char();\n",
    "            if (saw_signed) {\n"
    "                parsed_type = minic_type_signed_char();\n"
    "            } else if (saw_unsigned) {\n"
    "                parsed_type = minic_type_unsigned_char();\n"
    "            } else if (!minic_target_info_plain_char_type(parser->target_info, &parsed_type)) {\n"
    "                minic_parser_error(parser, \"cannot resolve target plain char type\");\n"
    "                return false;\n"
    "            }\n",
)

# Narrow string literals and __func__ use the same target-resolved char identity.
replace_exact(
    "src/frontend/parser_string.c",
    "    if (kind == MINIC_TOKEN_STRING_LITERAL) {\n"
    "        *type = minic_type_char();\n"
    "        return true;\n"
    "    }\n",
    "    if (kind == MINIC_TOKEN_STRING_LITERAL) {\n"
    "        return minic_target_info_plain_char_type(parser->target_info, type);\n"
    "    }\n",
)
replace_exact(
    "src/frontend/parser_string.c",
    "    MinicType array_type;\n"
    "    MinicType const_char_type;\n",
    "    MinicType array_type;\n"
    "    MinicType char_type;\n"
    "    MinicType const_char_type;\n",
)
replace_exact(
    "src/frontend/parser_string.c",
    "    if (!minic_type_add_const(minic_type_char(), &const_char_type) ||\n"
    "        !minic_c0_program_add_array_type(\n",
    "    if (!minic_target_info_plain_char_type(parser->target_info, &char_type) ||\n"
    "        !minic_type_add_const(char_type, &const_char_type) ||\n"
    "        !minic_c0_program_add_array_type(\n",
)

# Static string-pointer typing must use the same plain-char identity.
replace_exact(
    "src/frontend/parser_global.c",
    "    if (!minic_type_pointer_to(minic_type_char(), &string_pointer_type)) {\n",
    "    if (!minic_target_info_plain_char_type(parser->target_info, &string_pointer_type) ||\n"
    "        !minic_type_pointer_to(string_pointer_type, &string_pointer_type)) {\n",
)

# Reuse the existing uint64 scanner, but expose base/suffix syntax so TargetInfo
# can choose the C candidate type from base + suffix + value.
replace_exact(
    "src/frontend/parser_internal.h",
    "bool minic_parser_parse_unsigned_integer_value64(MinicParser *parser, uint64_t *value);\n",
    "bool minic_parser_parse_unsigned_integer_value64(MinicParser *parser, uint64_t *value);\n"
    "bool minic_parser_current_integer_literal_syntax(const MinicParser *parser,\n"
    "                                                 MinicIntegerLiteralBase *base,\n"
    "                                                 bool *has_unsigned_suffix,\n"
    "                                                 unsigned int *long_count);\n",
)
replace_exact(
    "src/frontend/parser_constant.c",
    "bool minic_parser_parse_unsigned_integer_value64(MinicParser *parser, uint64_t *value) {\n",
    "bool minic_parser_current_integer_literal_syntax(const MinicParser *parser,\n"
    "                                                 MinicIntegerLiteralBase *base,\n"
    "                                                 bool *has_unsigned_suffix,\n"
    "                                                 unsigned int *long_count) {\n"
    "    MinicSourceSpan span;\n"
    "    size_t digit_end;\n"
    "    size_t offset;\n"
    "    unsigned int parsed_long_count;\n"
    "    bool saw_unsigned;\n\n"
    "    if (parser == NULL || base == NULL || has_unsigned_suffix == NULL || long_count == NULL ||\n"
    "        parser->current.kind != MINIC_TOKEN_INTEGER_CONSTANT) {\n"
    "        return false;\n"
    "    }\n"
    "    span = parser->current.span;\n"
    "    digit_end = integer_digit_end(parser, span);\n"
    "    if (digit_end <= span.begin.offset) {\n"
    "        return false;\n"
    "    }\n"
    "    parsed_long_count = 0U;\n"
    "    saw_unsigned = false;\n"
    "    for (offset = digit_end; offset < span.end.offset; ++offset) {\n"
    "        char character;\n\n"
    "        character = parser->source[offset];\n"
    "        if (character == 'u' || character == 'U') {\n"
    "            if (saw_unsigned) {\n"
    "                return false;\n"
    "            }\n"
    "            saw_unsigned = true;\n"
    "        } else if (character == 'l' || character == 'L') {\n"
    "            parsed_long_count += 1U;\n"
    "            if (parsed_long_count > 2U) {\n"
    "                return false;\n"
    "            }\n"
    "        } else {\n"
    "            return false;\n"
    "        }\n"
    "    }\n"
    "    if (digit_end - span.begin.offset >= 2U && parser->source[span.begin.offset] == '0' &&\n"
    "        (parser->source[span.begin.offset + 1U] == 'x' ||\n"
    "         parser->source[span.begin.offset + 1U] == 'X')) {\n"
    "        *base = MINIC_INTEGER_LITERAL_BASE_HEXADECIMAL;\n"
    "    } else if (parser->source[span.begin.offset] == '0') {\n"
    "        *base = MINIC_INTEGER_LITERAL_BASE_OCTAL;\n"
    "    } else {\n"
    "        *base = MINIC_INTEGER_LITERAL_BASE_DECIMAL;\n"
    "    }\n"
    "    *has_unsigned_suffix = saw_unsigned;\n"
    "    *long_count = parsed_long_count;\n"
    "    return true;\n"
    "}\n\n"
    "bool minic_parser_parse_unsigned_integer_value64(MinicParser *parser, uint64_t *value) {\n",
)

# Replace suffix-only literal typing with target candidate selection.
text = read("src/frontend/parser_expression.c")
start = text.index("static MinicType integer_literal_type(")
end = text.index("static bool parse_integer(", start)
text = text[:start] + text[end:]
parse_start = text.index("static bool parse_integer(")
parse_end = text.index("static bool parse_floating(", parse_start)
new_parse = '''static bool parse_integer(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourceSpan span;
    MinicType literal_type;
    int64_t value;

    span = parser->current.span;
    if (parser->current.kind == MINIC_TOKEN_CHARACTER_CONSTANT) {
        int character_value;

        literal_type = minic_type_int();
        if (!minic_parser_parse_integer_value(parser, &character_value)) {
            return false;
        }
        value = (int64_t)character_value;
    } else {
        MinicIntegerLiteralBase base;
        uint64_t unsigned_value;
        unsigned int long_count;
        bool has_unsigned_suffix;

        if (!minic_parser_current_integer_literal_syntax(
                parser, &base, &has_unsigned_suffix, &long_count) ||
            !minic_parser_parse_unsigned_integer_value64(parser, &unsigned_value) ||
            !minic_target_info_integer_literal_type(parser->target_info,
                                                    base,
                                                    has_unsigned_suffix,
                                                    long_count,
                                                    unsigned_value,
                                                    &literal_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "integer constant has no representable target type");
            }
            return false;
        }
        (void)memcpy(&value, &unsigned_value, sizeof(value));
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = span;
    expression.type = literal_type;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = value;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

'''
write("src/frontend/parser_expression.c", text[:parse_start] + new_parse + text[parse_end:])

# Verifier checks plain-char sign against the active target instead of RV64-by-construction.
text = read("src/frontend/ast_verifier.c")
old_sig = "static bool type_is_valid(const MinicC0Program *program, MinicType type) {\n"
new_sig = "static bool type_is_valid(const MinicC0Program *program,\n                          const MinicTargetInfo *target,\n                          MinicType type) {\n"
if text.count(old_sig) != 1:
    raise SystemExit("ast_verifier.c: type_is_valid signature anchor mismatch")
text = text.replace(old_sig, new_sig)
old_plain = (
    "    if (type.is_plain_char && type.integer_sign != MINIC_INTEGER_SIGN_UNSIGNED) {\n"
    "        return false;\n"
    "    }\n"
    "    if (type.is_plain_char && type.integer_rank != MINIC_INTEGER_RANK_CHAR) {\n"
    "        return false;\n"
    "    }\n"
)
new_plain = (
    "    if (type.is_plain_char) {\n"
    "        MinicIntegerSign plain_char_sign;\n\n"
    "        if (type.integer_rank != MINIC_INTEGER_RANK_CHAR ||\n"
    "            !minic_target_info_plain_char_sign(target, &plain_char_sign) ||\n"
    "            type.integer_sign != plain_char_sign) {\n"
    "            return false;\n"
    "        }\n"
    "    }\n"
)
if text.count(old_plain) != 1:
    raise SystemExit("ast_verifier.c: plain-char invariant anchor mismatch")
text = text.replace(old_plain, new_plain)
count = text.count("type_is_valid(program, ")
if count == 0:
    raise SystemExit("ast_verifier.c: no type_is_valid consumers found")
text = text.replace("type_is_valid(program, ", "type_is_valid(program, target, ")
write("src/frontend/ast_verifier.c", text)

# Focused behavior regression for value-aware candidate selection.
replace_exact(
    "tests/compiler/c0/unsigned_64_literals.c",
    "typedef unsigned long long MiniU64;\n\n",
    "typedef unsigned long long MiniU64;\n\n"
    "_Static_assert(sizeof(2147483648) == sizeof(long),\n"
    "               \"decimal literal should select long after int overflow\");\n"
    "_Static_assert((0xffffffff >> 31) == 1,\n"
    "               \"hex literal should select unsigned int when signed int cannot represent it\");\n"
    "_Static_assert(sizeof(0x8000000000000000) == sizeof(unsigned long),\n"
    "               \"large hex literal should select unsigned long on RV64\");\n\n",
)

# Synthetic model proves plain-char type construction follows target policy too.
text = read("tests/target/riscv64/layout_test.c")
needle = "        MinicIntegerSign plain_char_sign;\n        MinicType promoted_type;\n"
if text.count(needle) != 2:
    raise SystemExit(f"layout_test.c: expected two integer-model blocks, found {text.count(needle)}")
# Protect the first (RV64) block and patch only the second synthetic block.
sentinel = "        MinicIntegerSign plain_char_sign;\n        /* integer-literal-staging-rv64 */\n        MinicType promoted_type;\n"
text = text.replace(needle, sentinel, 1)
needle2 = "        MinicIntegerSign plain_char_sign;\n        MinicType promoted_type;\n"
replacement2 = "        MinicIntegerSign plain_char_sign;\n        MinicType plain_char_type;\n        MinicType promoted_type;\n"
if text.count(needle2) != 1:
    raise SystemExit("layout_test.c: synthetic declaration block not unique")
text = text.replace(needle2, replacement2, 1)
old_check = (
    "        if (!minic_target_info_plain_char_sign(&target, &plain_char_sign) ||\n"
    "            plain_char_sign != MINIC_INTEGER_SIGN_SIGNED ||\n"
)
new_check = (
    "        if (!minic_target_info_plain_char_sign(&target, &plain_char_sign) ||\n"
    "            plain_char_sign != MINIC_INTEGER_SIGN_SIGNED ||\n"
    "            !minic_target_info_plain_char_type(&target, &plain_char_type) ||\n"
    "            !minic_type_is_plain_char(plain_char_type) ||\n"
    "            !minic_type_is_signed_integer(plain_char_type) ||\n"
)
if text.count(old_check) != 1:
    raise SystemExit("layout_test.c: synthetic plain-char check anchor mismatch")
text = text.replace(old_check, new_check)
text = text.replace(sentinel, needle, 1)
write("tests/target/riscv64/layout_test.c", text)

# Production code may only create unresolved plain-char identity inside type.c
# and the TargetInfo owner itself; every semantic consumer must ask TargetInfo.
for path in Path("src").rglob("*.c"):
    if path.as_posix() in ("src/frontend/type.c", "src/target/target_info.c"):
        continue
    if "minic_type_char()" in path.read_text():
        raise SystemExit(f"{path}: direct target-independent plain-char construction remains")

print("PASS materialize-target-integer-literals-v1")
