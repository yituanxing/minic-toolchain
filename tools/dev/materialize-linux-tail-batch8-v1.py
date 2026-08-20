from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}")
    file.write_text(text.replace(old, new, 1))


# 194: distinguish a local pointer-to-array object from a direct function pointer.
marker = "static bool parse_array_bound_allow_zero(MinicParser *parser, size_t *element_count);\n"
insert = r'''bool minic_parser_parse_parenthesized_pointer_to_array_declarator(
    MinicParser *parser,
    MinicType element_type,
    MinicSourceSpan *name_span,
    MinicType *declarator_type) {
    MinicType type;
    size_t pointer_depth;
    size_t level;
    unsigned int pointer_const_qualifiers;
    unsigned int pointer_volatile_qualifiers;
    bool is_array;

    if (parser == NULL || name_span == NULL || declarator_type == NULL ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before parenthesized pointer declarator")) {
        return false;
    }
    pointer_depth = 0U;
    pointer_const_qualifiers = 0U;
    pointer_volatile_qualifiers = 0U;
    while (parser->current.kind == MINIC_TOKEN_STAR) {
        pointer_depth += 1U;
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_pointer_qualifier_sequence(parser,
                                                           pointer_depth,
                                                           &pointer_const_qualifiers,
                                                           &pointer_volatile_qualifiers)) {
            return false;
        }
    }
    if (pointer_depth == 0U || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "pointer-to-array declarator requires a named pointer");
        return false;
    }
    *name_span = parser->current.span;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after parenthesized pointer declarator") ||
        !minic_parser_parse_array_declarator_suffix(
            parser, element_type, false, &type, &is_array) ||
        !is_array) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "parenthesized object pointer requires an array suffix");
        }
        return false;
    }
    for (level = 0U; level < pointer_depth; ++level) {
        unsigned int bit;

        if (!minic_type_pointer_to(type, &type)) {
            minic_parser_error(parser, "pointer-to-array declarator depth is unsupported");
            return false;
        }
        bit = 1U << level;
        if ((pointer_const_qualifiers & bit) != 0U &&
            !minic_type_add_const(type, &type)) {
            return false;
        }
        if ((pointer_volatile_qualifiers & bit) != 0U &&
            !minic_type_add_volatile(type, &type)) {
            return false;
        }
    }
    *declarator_type = type;
    return true;
}

'''
replace_once("src/frontend/parser_declarator.c", marker, insert + marker)

proto = r'''bool minic_parser_parse_parenthesized_function_declarator(
    MinicParser *parser,
    bool require_name,
    bool require_pointer,
    MinicParsedFunctionDeclarator *declarator);
'''
proto_new = proto + r'''bool minic_parser_parse_parenthesized_pointer_to_array_declarator(
    MinicParser *parser,
    MinicType element_type,
    MinicSourceSpan *name_span,
    MinicType *declarator_type);
'''
replace_once("src/frontend/parser_internal.h", proto, proto_new)

function_probe = r'''static bool local_declarator_starts_function_pointer(const MinicParser *parser) {
    MinicParser probe;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }
    probe = *parser;
    return minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_STAR;
}
'''
function_probe_new = function_probe + r'''
static bool local_declarator_starts_pointer_to_array(const MinicParser *parser) {
    MinicParser probe;
    size_t parenthesis_depth;

    if (!local_declarator_starts_function_pointer(parser)) {
        return false;
    }
    probe = *parser;
    parenthesis_depth = 0U;
    for (;;) {
        if (probe.current.kind == MINIC_TOKEN_LPAREN) {
            parenthesis_depth += 1U;
        } else if (probe.current.kind == MINIC_TOKEN_RPAREN) {
            if (parenthesis_depth == 0U) {
                return false;
            }
            parenthesis_depth -= 1U;
            if (parenthesis_depth == 0U) {
                return minic_parser_advance(&probe) &&
                       probe.current.kind == MINIC_TOKEN_LBRACKET;
            }
        } else if (probe.current.kind == MINIC_TOKEN_EOF) {
            return false;
        }
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
}
'''
replace_once("src/frontend/parser_statement.c", function_probe, function_probe_new)

branch = r'''    if (local_declarator_starts_function_pointer(parser)) {
        MinicParsedFunctionDeclarator declarator;
'''
branch_new = r'''    if (local_declarator_starts_pointer_to_array(parser)) {
        if (!minic_parser_parse_parenthesized_pointer_to_array_declarator(
                parser, declared_type, &local.name_span, &declared_type)) {
            return false;
        }
    } else if (local_declarator_starts_function_pointer(parser)) {
        MinicParsedFunctionDeclarator declarator;
'''
replace_once("src/frontend/parser_statement.c", branch, branch_new)

# 205: leading pointer layers belong to the function return type before parsing (*)(...).
old_type_name = r'''bool minic_parser_parse_type_name_preserving_incomplete(MinicParser *parser, MinicType *type) {
    MinicType base_type;

    if (parser == NULL || type == NULL || !minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }
    if (type_name_starts_parenthesized_function_pointer(parser)) {
        MinicParsedFunctionDeclarator declarator;

        if (!minic_parser_parse_parenthesized_function_declarator(
                parser, false, true, &declarator)) {
            return false;
        }
        if (declarator.is_variadic) {
            minic_parser_error(parser,
                               "variadic function-pointer type names are not supported yet");
            return false;
        }
        if (!minic_parser_build_function_declarator_type(parser, base_type, &declarator, type)) {
            minic_parser_error(parser, "cannot build function-pointer type name");
            return false;
        }
        return true;
    }
    {
        MinicType declarator_type;
        bool is_array;

        if (!minic_parser_parse_pointer_declarator(parser, base_type, &declarator_type) ||
            !minic_parser_parse_array_declarator_suffix(
                parser, declarator_type, true, type, &is_array)) {
            return false;
        }
        return true;
    }
}
'''
new_type_name = r'''bool minic_parser_parse_type_name_preserving_incomplete(MinicParser *parser, MinicType *type) {
    MinicType base_type;
    MinicType declarator_base;

    if (parser == NULL || type == NULL || !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, &declarator_base)) {
        return false;
    }
    if (type_name_starts_parenthesized_function_pointer(parser)) {
        MinicParsedFunctionDeclarator declarator;

        if (!minic_parser_parse_parenthesized_function_declarator(
                parser, false, true, &declarator)) {
            return false;
        }
        if (declarator.is_variadic) {
            minic_parser_error(parser,
                               "variadic function-pointer type names are not supported yet");
            return false;
        }
        if (!minic_parser_build_function_declarator_type(
                parser, declarator_base, &declarator, type)) {
            minic_parser_error(parser, "cannot build function-pointer type name");
            return false;
        }
        return true;
    }
    {
        bool is_array;

        if (!minic_parser_parse_array_declarator_suffix(
                parser, declarator_base, true, type, &is_array)) {
            return false;
        }
        return true;
    }
}
'''
replace_once("src/frontend/parser_type.c", old_type_name, new_type_name)

# 246: GNU Linux idiom (integer)((typed_pointer)0) remains an integer constant expression.
const_marker = r'''static bool eval_expression(const MinicC0Program *program,
                            const MinicTargetInfo *target,
                            MinicExpressionId expression_id,
                            unsigned int depth,
                            MinicConstValue *value);
'''
const_helper = r'''static bool integer_cast_operand_is_null_pointer(const MinicC0Program *program,
                                                const MinicExpression *expression) {
    const MinicExpression *pointer_cast;
    const MinicExpression *zero;

    if (program == NULL || expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    pointer_cast = minic_c0_program_expression(program, expression->value.unary.operand);
    if (pointer_cast == NULL || !minic_type_is_pointer(pointer_cast->type) ||
        (pointer_cast->kind != MINIC_EXPRESSION_CAST &&
         pointer_cast->kind != MINIC_EXPRESSION_BITCAST)) {
        return false;
    }
    zero = minic_c0_program_expression(program, pointer_cast->value.unary.operand);
    return zero != NULL && zero->kind == MINIC_EXPRESSION_INTEGER &&
           minic_type_is_integer(zero->type) && zero->value.integer_value == 0;
}

'''
replace_once("src/frontend/const_eval.c", const_marker, const_helper + const_marker)

old_cast = r'''    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_CONVERSION: {
        MinicConstValue operand;

        return eval_expression(
                   program, target, expression->value.unary.operand, depth + 1U, &operand) &&
               convert_value(program, target, &operand, expression->type, value);
    }
'''
new_cast = r'''    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_CONVERSION: {
        MinicConstValue operand;

        if (expression->kind == MINIC_EXPRESSION_CAST &&
            integer_cast_operand_is_null_pointer(program, expression)) {
            value->type = expression->type;
            return normalize_bits(program, target, expression->type, 0U, &value->bits);
        }
        return eval_expression(
                   program, target, expression->value.unary.operand, depth + 1U, &operand) &&
               convert_value(program, target, &operand, expression->type, value);
    }
'''
replace_once("src/frontend/const_eval.c", old_cast, new_cast)

# 252: relational pointer comparison requires compatible object pointees, not pointer-arithmetic completeness.
old_rel = r'''    return program != NULL && minic_type_pointee(left, &left_pointee) &&
           minic_type_pointee(right, &right_pointee) &&
           minic_type_unqualified(left_pointee, &left_unqualified) &&
           minic_type_unqualified(right_pointee, &right_unqualified) &&
           minic_type_equal(left_unqualified, right_unqualified) &&
           minic_c0_pointer_arithmetic_pointee_allowed(program, left_unqualified) &&
           minic_c0_pointer_arithmetic_pointee_allowed(program, right_unqualified);
'''
new_rel = r'''    return program != NULL && minic_type_pointee(left, &left_pointee) &&
           minic_type_pointee(right, &right_pointee) &&
           minic_type_unqualified(left_pointee, &left_unqualified) &&
           minic_type_unqualified(right_pointee, &right_unqualified) &&
           minic_type_equal(left_unqualified, right_unqualified) &&
           !minic_type_is_void(left_unqualified) && !minic_type_is_function(left_unqualified);
'''
replace_once("src/frontend/ast.c", old_rel, new_rel)

# Keep the focused batch in the permanent C0 gate.
run = Path("tests/compiler/c0/run.sh")
run_text = run.read_text()
if "run-linux-tail-batch8.sh" not in run_text:
    run_text += r'''

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-linux-tail-batch8.sh"
'''
    run.write_text(run_text)
