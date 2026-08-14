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
        raise SystemExit(f"{path}: expected {count} occurrences, found {actual}: {old[:80]!r}")
    write(path, text.replace(old, new))


def require_absent(path, needle):
    if needle in read(path):
        raise SystemExit(f"{path}: unexpected remaining text: {needle}")


# TargetInfo owns the target-dependent plain-char semantic type.
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

# MinicType validates plain-char identity, but TargetInfo decides its sign.
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

# Parser type construction routes unsuffixed plain char through TargetInfo.
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

# Narrow strings and __func__ use the same target-owned plain-char type.
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
replace_exact(
    "src/frontend/parser_global.c",
    "    if (!minic_type_pointer_to(minic_type_char(), &string_pointer_type)) {\n",
    "    if (!minic_target_info_plain_char_type(parser->target_info, &string_pointer_type) ||\n"
    "        !minic_type_pointer_to(string_pointer_type, &string_pointer_type)) {\n",
)

# Reuse the existing integer scanner; expose only base/suffix syntax to expression typing.
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

# Integer literal type selection now depends on base, suffix, value and target model.
text = read("src/frontend/parser_expression.c")
start = text.index("static MinicType integer_literal_type(")
end = text.index("static bool parse_integer(", start)
text = text[:start] + text[end:]
old_parse_integer_start = text.index("static bool parse_integer(")
old_parse_integer_end = text.index("static bool parse_floating(", old_parse_integer_start)
new_parse_integer = '''static bool parse_integer(MinicParser *parser, MinicExpressionId *expression_id) {
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
text = text[:old_parse_integer_start] + new_parse_integer + text[old_parse_integer_end:]
write("src/frontend/parser_expression.c", text)
replace_exact(
    "src/frontend/parser_expression.c",
    "!minic_type_integer_common(index_type, stride.type, &scaled_type)",
    "!minic_target_info_integer_common(parser->target_info, index_type, stride.type, &scaled_type)",
)
replace_exact(
    "src/frontend/parser_expression.c",
    "!minic_type_integer_common(\n                operand_expression->type, operand_expression->type, &expression.type)",
    "!minic_target_info_integer_promotion(\n                parser->target_info, operand_expression->type, &expression.type)",
    count=3,
)
replace_exact(
    "src/frontend/parser_expression.c",
    "static bool binary_result_type(const MinicC0Program *program,\n"
    "                               MinicTokenKind kind,\n",
    "static bool binary_result_type(const MinicC0Program *program,\n"
    "                               const MinicTargetInfo *target,\n"
    "                               MinicTokenKind kind,\n",
)
replace_exact(
    "src/frontend/parser_expression.c",
    "            return minic_type_integer_common(left, left, result);\n"
    "        }\n"
    "        return minic_type_integer_common(left, right, result);\n",
    "            return minic_target_info_integer_promotion(target, left, result);\n"
    "        }\n"
    "        return minic_target_info_integer_common(target, left, right, result);\n",
)
replace_exact(
    "src/frontend/parser_expression.c",
    "        } else if (!binary_result_type(parser->program,\n"
    "                                       token_kind,\n",
    "        } else if (!binary_result_type(parser->program,\n"
    "                                       parser->target_info,\n"
    "                                       token_kind,\n",
)
replace_exact(
    "src/frontend/parser_expression.c",
    "!minic_type_integer_common(target_type, value_expression->type, &common_type)",
    "!minic_target_info_integer_common(\n                    parser->target_info, target_type, value_expression->type, &common_type)",
)
replace_exact(
    "src/frontend/parser_expression.c",
    "        if (!minic_c0_conditional_result_type(\n"
    "                parser->program, when_true, when_false, &conditional.type)) {\n",
    "        if (!minic_c0_conditional_result_type(\n"
    "                parser->program,\n"
    "                parser->target_info,\n"
    "                when_true,\n"
    "                when_false,\n"
    "                &conditional.type)) {\n",
)
require_absent("src/frontend/parser_expression.c", "minic_type_integer_common(")

# Statement semantic lowering uses the same target-owned common-type rules.
text = read("src/frontend/parser_statement.c")
if text.count("minic_type_integer_common(") != 5:
    raise SystemExit("parser_statement.c: unexpected legacy integer-common call count")
text = text.replace(
    "minic_type_integer_common(", "minic_target_info_integer_common(parser->target_info, "
)
write("src/frontend/parser_statement.c", text)

# Conditional semantic typing is target-aware without introducing an AST -> TargetInfo include cycle.
replace_exact(
    "src/frontend/ast.h",
    "#define MINIC_CLEANUP_CONTEXT_ROOT ((MinicCleanupContextId)0)\n",
    "#define MINIC_CLEANUP_CONTEXT_ROOT ((MinicCleanupContextId)0)\n\n"
    "struct MinicTargetInfo;\n",
)
replace_exact(
    "src/frontend/ast.h",
    "bool minic_c0_conditional_result_type(const MinicC0Program *program,\n"
    "                                      MinicExpressionId when_true_expression_id,\n",
    "bool minic_c0_conditional_result_type(const MinicC0Program *program,\n"
    "                                      const struct MinicTargetInfo *target,\n"
    "                                      MinicExpressionId when_true_expression_id,\n",
)
replace_exact(
    "src/frontend/ast.c",
    "#include \"frontend/ast.h\"\n\n",
    "#include \"frontend/ast.h\"\n#include \"target/target_info.h\"\n\n",
)
replace_exact(
    "src/frontend/ast.c",
    "bool minic_c0_conditional_result_type(const MinicC0Program *program,\n"
    "                                      MinicExpressionId when_true_expression_id,\n",
    "bool minic_c0_conditional_result_type(const MinicC0Program *program,\n"
    "                                      const struct MinicTargetInfo *target,\n"
    "                                      MinicExpressionId when_true_expression_id,\n",
)
replace_exact(
    "src/frontend/ast.c",
    "    if (program == NULL || result == NULL) {\n",
    "    if (program == NULL || target == NULL || result == NULL) {\n",
    count=1,
)
replace_exact(
    "src/frontend/ast.c",
    "        return minic_type_integer_common(when_true, when_false, result);\n",
    "        return minic_target_info_integer_common(target, when_true, when_false, result);\n",
)

# Verifier validates target-resolved plain char and recomputes integer semantics through TargetInfo.
replace_exact(
    "src/frontend/ast_verifier.c",
    "static bool type_is_valid(const MinicC0Program *program, MinicType type) {\n",
    "static bool type_is_valid(const MinicC0Program *program,\n"
    "                          const MinicTargetInfo *target,\n"
    "                          MinicType type) {\n",
)
replace_exact(
    "src/frontend/ast_verifier.c",
    "    if (type.is_plain_char && type.integer_sign != MINIC_INTEGER_SIGN_UNSIGNED) {\n"
    "        return false;\n"
    "    }\n"
    "    if (type.is_plain_char && type.integer_rank != MINIC_INTEGER_RANK_CHAR) {\n"
    "        return false;\n"
    "    }\n",
    "    if (type.is_plain_char) {\n"
    "        MinicIntegerSign plain_char_sign;\n\n"
    "        if (type.integer_rank != MINIC_INTEGER_RANK_CHAR ||\n"
    "            !minic_target_info_plain_char_sign(target, &plain_char_sign) ||\n"
    "            type.integer_sign != plain_char_sign) {\n"
    "            return false;\n"
    "        }\n"
    "    }\n",
)
replace_exact(
    "src/frontend/ast_verifier.c",
    "static bool verify_binary_type(const MinicC0Program *program,\n"
    "                               const MinicExpression *expression,\n",
    "static bool verify_binary_type(const MinicC0Program *program,\n"
    "                               const MinicTargetInfo *target,\n"
    "                               const MinicExpression *expression,\n",
)
replace_exact(
    "src/frontend/ast_verifier.c",
    "            if (!minic_type_integer_promotion(left->type, &expected_type)) {\n",
    "            if (!minic_target_info_integer_promotion(target, left->type, &expected_type)) {\n",
)
replace_exact(
    "src/frontend/ast_verifier.c",
    "        } else if (!minic_type_integer_common(left->type, right->type, &expected_type)) {\n",
    "        } else if (!minic_target_info_integer_common(\n"
    "                       target, left->type, right->type, &expected_type)) {\n",
)
text = read("src/frontend/ast_verifier.c")
if text.count("type_is_valid(program, ") != 7:
    raise SystemExit(f"ast_verifier.c: expected 7 type_is_valid calls, found {text.count('type_is_valid(program, ')}")
text = text.replace("type_is_valid(program, ", "type_is_valid(program, target, ")
write("src/frontend/ast_verifier.c", text)
replace_exact(
    "src/frontend/ast_verifier.c",
    "               verify_binary_type(program, expression, left, right, form);\n",
    "               verify_binary_type(program, target, expression, left, right, form);\n",
)
replace_exact(
    "src/frontend/ast_verifier.c",
    "               minic_c0_conditional_result_type(program,\n"
    "                                                expression->value.conditional.when_true,\n",
    "               minic_c0_conditional_result_type(program,\n"
    "                                                target,\n"
    "                                                expression->value.conditional.when_true,\n",
)
require_absent("src/frontend/ast_verifier.c", "minic_type_integer_common(")
require_absent("src/frontend/ast_verifier.c", "minic_type_integer_promotion(")

# RV64 backend is explicitly target-specific; any remaining retyping uses the canonical RV64 TargetInfo.
text = read("src/target/riscv64/codegen_expression.c")
if text.count("minic_type_integer_common(") != 2:
    raise SystemExit("codegen_expression.c: unexpected legacy integer-common call count")
text = text.replace(
    "minic_type_integer_common(",
    "minic_target_info_integer_common(minic_default_target_info(), ",
)
write("src/target/riscv64/codegen_expression.c", text)
replace_exact(
    "src/target/riscv64/codegen_statement.c",
    "#include \"target/riscv64/codegen_internal.h\"\n\n",
    "#include \"target/riscv64/codegen_internal.h\"\n#include \"target/target_info.h\"\n\n",
)
replace_exact(
    "src/target/riscv64/codegen_statement.c",
    "minic_type_integer_common(target->type, value->type, &common_type)",
    "minic_target_info_integer_common(\n            minic_default_target_info(), target->type, value->type, &common_type)",
)

# Focused regression: these unsuffixed constants require value-aware C candidate selection.
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

# Prove the plain-char type helper follows the synthetic model too.
replace_exact(
    "tests/target/riscv64/layout_test.c",
    "        MinicIntegerSign plain_char_sign;\n"
    "        MinicType promoted_type;\n",
    "        MinicIntegerSign plain_char_sign;\n"
    "        MinicType plain_char_type;\n"
    "        MinicType promoted_type;\n",
)
replace_exact(
    "tests/target/riscv64/layout_test.c",
    "        if (!minic_target_info_plain_char_sign(&target, &plain_char_sign) ||\n"
    "            plain_char_sign != MINIC_INTEGER_SIGN_SIGNED ||\n",
    "        if (!minic_target_info_plain_char_sign(&target, &plain_char_sign) ||\n"
    "            plain_char_sign != MINIC_INTEGER_SIGN_SIGNED ||\n"
    "            !minic_target_info_plain_char_type(&target, &plain_char_type) ||\n"
    "            !minic_type_is_plain_char(plain_char_type) ||\n"
    "            !minic_type_is_signed_integer(plain_char_type) ||\n",
)

# The legacy type-only algorithms may remain for the isolated type unit test, but production src
# must no longer consume them. Also ensure plain char construction is target-owned outside type.c.
for path in Path("src").rglob("*.c"):
    if path.as_posix() == "src/frontend/type.c":
        continue
    text = path.read_text()
    if "minic_type_integer_common(" in text or "minic_type_integer_promotion(" in text:
        raise SystemExit(f"{path}: legacy integer semantic helper remains in production")
    if "minic_type_char()" in text:
        raise SystemExit(f"{path}: target-independent plain-char construction remains in production")

print("PASS materialize-target-integer-consumers-v1")
