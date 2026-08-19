#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"unexpected shape in {path}: {old[:80]!r}")
    path.write_text(text.replace(old, new, 1))


ast = Path("src/frontend/ast.h")
replace_once(
    ast,
    "    MINIC_EXPRESSION_BUILTIN_VA_START,\n    MINIC_EXPRESSION_BUILTIN_VA_END,\n",
    "    MINIC_EXPRESSION_BUILTIN_VA_START,\n    MINIC_EXPRESSION_BUILTIN_VA_COPY,\n    MINIC_EXPRESSION_BUILTIN_VA_END,\n",
)

traversal = Path("src/frontend/ast_traversal.c")
replace_once(
    traversal,
    "    case MINIC_EXPRESSION_ASSIGNMENT:\n    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT:\n    case MINIC_EXPRESSION_BINARY:\n",
    "    case MINIC_EXPRESSION_ASSIGNMENT:\n    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT:\n    case MINIC_EXPRESSION_BINARY:\n    case MINIC_EXPRESSION_BUILTIN_VA_COPY:\n",
)

parser = Path("src/frontend/parser_expression.c")
copy_parser = r'''static bool parse_builtin_va_copy(MinicParser *parser, MinicExpressionId *expression_id) {
    const MinicExpression *source;
    const MinicExpression *target;
    MinicExpression expression;
    MinicExpressionId source_id;
    MinicExpressionId target_id;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicSourceSpan target_span;

    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_va_copy") ||
        !parse_builtin_va_list_target(parser, &target_id, &target_span) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_va_copy") ||
        !parse_expression_internal(parser, &source_id, 0U, false)) {
        return false;
    }
    (void)target_span;

    target = minic_c0_program_expression(parser->program, target_id);
    source = minic_c0_program_expression(parser->program, source_id);
    if (target == NULL || source == NULL || source->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(source->type)) {
        minic_parser_error(parser, "__builtin_va_copy source must be a va_list lvalue");
        return false;
    }
    if (!minic_c0_types_compatible(parser->program, target->type, source->type)) {
        minic_parser_error(parser, "__builtin_va_copy source type must match destination type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_va_copy");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_VA_COPY;
    expression.span.begin = begin;
    expression.span.end = end;
    expression.type = minic_type_void();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.binary.left = target_id;
    expression.value.binary.right = source_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

'''
replace_once(
    parser,
    "static bool parse_builtin_va_end(MinicParser *parser, MinicExpressionId *expression_id) {\n",
    copy_parser + "static bool parse_builtin_va_end(MinicParser *parser, MinicExpressionId *expression_id) {\n",
)
replace_once(
    parser,
    '''    if (generic_token_text_equals(parser, "__builtin_va_end")) {
        if (!parse_builtin_va_end(parser, &primary_id)) {
''',
    '''    if (generic_token_text_equals(parser, "__builtin_va_copy")) {
        if (!parse_builtin_va_copy(parser, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_va_end")) {
        if (!parse_builtin_va_end(parser, &primary_id)) {
''',
)

verifier = Path("src/frontend/ast_verifier.c")
copy_verifier = r'''    case MINIC_EXPRESSION_BUILTIN_VA_COPY:
        left = expression_before(program, expression->value.binary.left, expression_index);
        right = expression_before(program, expression->value.binary.right, expression_index);
        return left != NULL && right != NULL && left->value_category == MINIC_VALUE_LVALUE &&
               right->value_category == MINIC_VALUE_LVALUE && minic_type_is_pointer(left->type) &&
               minic_type_is_pointer(right->type) && !minic_type_is_const(left->type) &&
               minic_c0_types_compatible(program, left->type, right->type) &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_is_void(expression->type);
'''
replace_once(
    verifier,
    "    case MINIC_EXPRESSION_BUILTIN_UNARY:\n",
    copy_verifier + "    case MINIC_EXPRESSION_BUILTIN_UNARY:\n",
)

codegen = Path("src/target/riscv64/codegen_expression.c")
copy_codegen = r'''    case MINIC_EXPRESSION_BUILTIN_VA_COPY: {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicExpressionId source_id;
        MinicExpressionId target_id;

        target_id = expression->value.binary.left;
        source_id = expression->value.binary.right;
        target = minic_c0_program_expression(program, target_id);
        source = minic_c0_program_expression(program, source_id);
        if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            source->value_category != MINIC_VALUE_LVALUE || !minic_type_is_pointer(target->type) ||
            !minic_type_is_pointer(source->type) || minic_type_is_const(target->type) ||
            !minic_c0_types_compatible(program, target->type, source->type) ||
            !minic_riscv64_emit_lvalue_address(
                file, program, function, function_layout, target_id) ||
            fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
            !minic_riscv64_emit_expression(
                file, program, function, function_layout, source_id) ||
            fprintf(file, "  ld t4, 0(sp)\n  addi sp, sp, 16\n") < 0) {
            return false;
        }
        return minic_riscv64_emit_lvalue_store_to_address(
            file, program, target_id, target->type, "a0", "t4");
    }
'''
replace_once(
    codegen,
    "    case MINIC_EXPRESSION_BUILTIN_VA_END: {\n",
    copy_codegen + "    case MINIC_EXPRESSION_BUILTIN_VA_END: {\n",
)

run = Path("tests/compiler/c0/run.sh")
marker = '''MINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-declaration-head-variadic.sh"
'''
replace_once(
    run,
    marker,
    marker
    + '''
MINIC="$minic" \\
RISCV_CC="${RISCV_CC:-riscv64-linux-gnu-gcc}" \\
QEMU_RISCV64="${QEMU_RISCV64:-qemu-riscv64}" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-builtin-va-copy.sh"
''',
)

print("materialized target-aware GNU __builtin_va_copy")
