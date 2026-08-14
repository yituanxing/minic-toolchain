#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


# Dedicated semantic node: overflow builtins both store through their third
# operand and produce a boolean result. Keeping this effect explicit avoids
# pretending they are ordinary arithmetic or library calls and gives Core IR a
# clean lowering point later.
path = Path("src/frontend/ast.h")
text = path.read_text()
kind_anchor = "    MINIC_EXPRESSION_STATEMENT\n} MinicExpressionKind;"
if kind_anchor not in text:
    kind_anchor = "    MINIC_EXPRESSION_STATEMENT,\n"
    if kind_anchor not in text:
        raise SystemExit("overflow builtin: cannot locate staged statement-expression kind")
    text = text.replace(kind_anchor,
                        "    MINIC_EXPRESSION_STATEMENT,\n    MINIC_EXPRESSION_BUILTIN_OVERFLOW,\n",
                        1)
else:
    text = text.replace(kind_anchor,
                        "    MINIC_EXPRESSION_STATEMENT,\n    MINIC_EXPRESSION_BUILTIN_OVERFLOW\n} MinicExpressionKind;",
                        1)

binary_enum_end = "} MinicBinaryOperator;\n"
overflow_enum = r'''
typedef enum MinicOverflowOperator {
    MINIC_OVERFLOW_ADD = 0,
    MINIC_OVERFLOW_SUBTRACT,
    MINIC_OVERFLOW_MULTIPLY
} MinicOverflowOperator;
'''
if text.count(binary_enum_end) != 1:
    raise SystemExit("overflow builtin: cannot locate binary operator enum")
text = text.replace(binary_enum_end, binary_enum_end + overflow_enum, 1)

payload_anchor = r'''        struct {
            MinicBlockId block;
            MinicExpressionId result;
        } statement_expression;
'''
overflow_payload = r'''        struct {
            MinicOverflowOperator operator_kind;
            MinicExpressionId left;
            MinicExpressionId right;
            MinicExpressionId result_pointer;
        } overflow;
'''
if text.count(payload_anchor) != 1:
    raise SystemExit(f"overflow builtin: expected one statement-expression payload, found {text.count(payload_anchor)}")
Path("src/frontend/ast.h").write_text(text.replace(payload_anchor, payload_anchor + overflow_payload, 1))

# Parser/Sema bootstrap. Current real-source subset requires both operands and
# the result pointee to have the same non-_Bool integer type. GCC is more general
# (arbitrary integral operands); that widening is an explicit later Sema step,
# not a silent approximation here.
path = Path("src/frontend/parser_expression.c")
text = path.read_text()
marker = "static bool parse_primary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {\n"
if text.count(marker) != 1:
    raise SystemExit("overflow builtin: cannot locate parse_primary")
helper = r'''static bool parse_builtin_overflow(MinicParser *parser,
                                   MinicOverflowOperator operator_kind,
                                   MinicExpressionId *expression_id) {
    MinicExpression expression;
    const MinicExpression *left;
    const MinicExpression *right;
    const MinicExpression *result_pointer;
    MinicExpressionId left_id;
    MinicExpressionId right_id;
    MinicExpressionId result_pointer_id;
    MinicSourcePosition begin;
    MinicType result_type;

    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after overflow builtin") ||
        !parse_expression_internal(parser, &left_id, 0U, true) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected first ',' in overflow builtin") ||
        !parse_expression_internal(parser, &right_id, 0U, true) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected second ',' in overflow builtin") ||
        !parse_expression_internal(parser, &result_pointer_id, 0U, true) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after overflow builtin")) {
        return false;
    }

    left = minic_c0_program_expression(parser->program, left_id);
    right = minic_c0_program_expression(parser->program, right_id);
    result_pointer = minic_c0_program_expression(parser->program, result_pointer_id);
    if (left == NULL || right == NULL || result_pointer == NULL ||
        !minic_type_pointee(result_pointer->type, &result_type) ||
        !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||
        !minic_type_equal(left->type, result_type) || !minic_type_equal(right->type, result_type)) {
        minic_parser_error(parser,
                           "overflow builtin currently requires matching non-bool integer operands and result pointee");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_OVERFLOW;
    expression.span.begin = begin;
    expression.span.end = parser->previous.span.end;
    expression.type = minic_type_bool();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.overflow.operator_kind = operator_kind;
    expression.value.overflow.left = left_id;
    expression.value.overflow.right = right_id;
    expression.value.overflow.result_pointer = result_pointer_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

'''
text = text.replace(marker, helper + marker, 1)
entry = '''    if (generic_token_text_equals(parser, "__builtin_types_compatible_p")) {
'''
replacement = '''    if (generic_token_text_equals(parser, "__builtin_add_overflow")) {
        if (!parse_builtin_overflow(parser, MINIC_OVERFLOW_ADD, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_sub_overflow")) {
        if (!parse_builtin_overflow(parser, MINIC_OVERFLOW_SUBTRACT, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_mul_overflow")) {
        if (!parse_builtin_overflow(parser, MINIC_OVERFLOW_MULTIPLY, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_types_compatible_p")) {
'''
if text.count(entry) != 1:
    raise SystemExit(f"overflow builtin: expected one compile-time builtin entry, found {text.count(entry)}")
path.write_text(text.replace(entry, replacement, 1))

# Cast normalization must remap all three semantic edges.
path = Path("src/frontend/cast_normalization.c")
text = path.read_text()
anchor = "    case MINIC_EXPRESSION_STATEMENT:\n"
case = r'''    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:
        return remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.overflow.left,
                                   &expression->value.overflow.left) &&
               remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.overflow.right,
                                   &expression->value.overflow.right) &&
               remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.overflow.result_pointer,
                                   &expression->value.overflow.result_pointer);
'''
if text.count(anchor) != 1:
    raise SystemExit(f"overflow builtin: expected one normalization statement case, found {text.count(anchor)}")
path.write_text(text.replace(anchor, case + anchor, 1))

# Verifier freezes the frontend invariant independently of parser success.
path = Path("src/frontend/ast_verifier.c")
text = path.read_text()
anchor = "    case MINIC_EXPRESSION_STATEMENT: {\n"
case = r'''    case MINIC_EXPRESSION_BUILTIN_OVERFLOW: {
        const MinicExpression *left;
        const MinicExpression *right;
        const MinicExpression *result_pointer;
        MinicType result_type;

        left = expression_before(program, expression->value.overflow.left, expression_index);
        right = expression_before(program, expression->value.overflow.right, expression_index);
        result_pointer = expression_before(
            program, expression->value.overflow.result_pointer, expression_index);
        return left != NULL && right != NULL && result_pointer != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_bool()) &&
               expression->value.overflow.operator_kind >= MINIC_OVERFLOW_ADD &&
               expression->value.overflow.operator_kind <= MINIC_OVERFLOW_MULTIPLY &&
               minic_type_pointee(result_pointer->type, &result_type) &&
               minic_type_is_integer(result_type) && !minic_type_is_bool_integer(result_type) &&
               minic_type_equal(left->type, result_type) &&
               minic_type_equal(right->type, result_type);
    }
'''
if text.count(anchor) != 1:
    raise SystemExit(f"overflow builtin: expected one verifier statement-expression case, found {text.count(anchor)}")
path.write_text(text.replace(anchor, case + anchor, 1))

# RV64 lowering implements exact same-type integer semantics up to XLEN. For
# sub-XLEN types, doing the operation in XLEN then comparing against the
# converted result detects overflow exactly. XLEN operations use standard
# signed/unsigned overflow identities and mulh/mulhu.
path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text()
marker = "bool minic_riscv64_emit_expression(FILE *file,\n"
if text.count(marker) != 1:
    raise SystemExit(f"overflow builtin: expected one emit_expression definition, found {text.count(marker)}")
helper = r'''static bool minic_riscv64_emit_overflow_builtin(FILE *file,
                                                  const MinicC0Program *program,
                                                  const MinicFunction *function,
                                                  const MinicExpression *expression) {
    const MinicExpression *left;
    const MinicExpression *right;
    const MinicExpression *result_pointer;
    MinicType result_type;
    size_t result_size;
    size_t result_alignment;
    bool is_unsigned;

    if (expression == NULL || expression->kind != MINIC_EXPRESSION_BUILTIN_OVERFLOW) {
        return false;
    }
    left = minic_c0_program_expression(program, expression->value.overflow.left);
    right = minic_c0_program_expression(program, expression->value.overflow.right);
    result_pointer = minic_c0_program_expression(program, expression->value.overflow.result_pointer);
    if (left == NULL || right == NULL || result_pointer == NULL ||
        !minic_type_pointee(result_pointer->type, &result_type) ||
        !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||
        !minic_type_equal(left->type, result_type) || !minic_type_equal(right->type, result_type) ||
        !minic_riscv64_type_layout(program, result_type, &result_size, &result_alignment) ||
        result_size == 0U || result_size > 8U) {
        return false;
    }
    (void)result_alignment;
    is_unsigned = minic_type_is_unsigned_integer(result_type);

    if (!minic_riscv64_emit_expression(file, program, function, expression->value.overflow.left) ||
        !minic_riscv64_emit_integer_conversion(file, result_type, "a0") ||
        !minic_riscv64_emit_stack_allocate(file, 16U) ||
        fprintf(file, "  sd a0, 0(sp)\n") < 0 ||
        !minic_riscv64_emit_expression(file, program, function, expression->value.overflow.right) ||
        !minic_riscv64_emit_integer_conversion(file, result_type, "a0") ||
        !minic_riscv64_emit_stack_allocate(file, 16U) ||
        fprintf(file, "  sd a0, 0(sp)\n") < 0 ||
        !minic_riscv64_emit_expression(
            file, program, function, expression->value.overflow.result_pointer) ||
        fprintf(file,
                "  mv t3, a0\n"
                "  ld t1, 0(sp)\n"
                "  ld t0, 16(sp)\n") < 0 ||
        !minic_riscv64_emit_stack_release(file, 32U)) {
        return false;
    }

    if (result_size < 8U) {
        const char *instruction;

        instruction = expression->value.overflow.operator_kind == MINIC_OVERFLOW_ADD
                          ? "add"
                          : expression->value.overflow.operator_kind == MINIC_OVERFLOW_SUBTRACT
                                ? "sub"
                                : "mul";
        if (fprintf(file, "  %s t2, t0, t1\n  mv t4, t2\n", instruction) < 0 ||
            !minic_riscv64_emit_integer_conversion(file, result_type, "t2") ||
            fprintf(file, "  xor t4, t4, t2\n  snez a0, t4\n") < 0) {
            return false;
        }
    } else if (is_unsigned) {
        switch (expression->value.overflow.operator_kind) {
        case MINIC_OVERFLOW_ADD:
            if (fprintf(file, "  add t2, t0, t1\n  sltu a0, t2, t0\n") < 0) {
                return false;
            }
            break;
        case MINIC_OVERFLOW_SUBTRACT:
            if (fprintf(file, "  sub t2, t0, t1\n  sltu a0, t0, t1\n") < 0) {
                return false;
            }
            break;
        case MINIC_OVERFLOW_MULTIPLY:
            if (fprintf(file,
                        "  mul t2, t0, t1\n"
                        "  mulhu t4, t0, t1\n"
                        "  snez a0, t4\n") < 0) {
                return false;
            }
            break;
        }
    } else {
        switch (expression->value.overflow.operator_kind) {
        case MINIC_OVERFLOW_ADD:
            if (fprintf(file,
                        "  add t2, t0, t1\n"
                        "  xor t4, t0, t2\n"
                        "  xor t5, t1, t2\n"
                        "  and t4, t4, t5\n"
                        "  slt a0, t4, zero\n") < 0) {
                return false;
            }
            break;
        case MINIC_OVERFLOW_SUBTRACT:
            if (fprintf(file,
                        "  sub t2, t0, t1\n"
                        "  xor t4, t0, t1\n"
                        "  xor t5, t0, t2\n"
                        "  and t4, t4, t5\n"
                        "  slt a0, t4, zero\n") < 0) {
                return false;
            }
            break;
        case MINIC_OVERFLOW_MULTIPLY:
            if (fprintf(file,
                        "  mul t2, t0, t1\n"
                        "  mulh t4, t0, t1\n"
                        "  srai t5, t2, 63\n"
                        "  xor t4, t4, t5\n"
                        "  snez a0, t4\n") < 0) {
                return false;
            }
            break;
        }
    }
    return minic_riscv64_emit_scalar_store(file, result_type, "t2", "t3");
}

'''
text = text.replace(marker, helper + marker, 1)
anchor = "    case MINIC_EXPRESSION_STATEMENT: {\n"
case = r'''    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:
        return minic_riscv64_emit_overflow_builtin(file, program, function, expression);
'''
if text.count(anchor) != 1:
    raise SystemExit(f"overflow builtin: expected one codegen statement-expression case, found {text.count(anchor)}")
path.write_text(text.replace(anchor, case + anchor, 1))

print("staged GNU add/sub/mul overflow builtins with explicit store+bool AST semantics and exact RV64 <=XLEN lowering")
