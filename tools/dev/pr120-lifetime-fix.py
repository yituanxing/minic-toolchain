from pathlib import Path


path = Path("src/frontend/parser_expression.c")
text = path.read_text()
old = '''    const MinicExpression *index_expression;
    const MinicArrayType *nested_array;
    MinicExpression stride;
    MinicExpression scaled;
    MinicType selected_type;
    MinicType scaled_type;
    size_t element_size;
'''
new = '''    const MinicExpression *index_expression;
    const MinicArrayType *nested_array;
    MinicExpression stride;
    MinicExpression scaled;
    MinicSourceSpan index_span;
    MinicType index_type;
    MinicType selected_type;
    MinicType scaled_type;
    size_t element_size;
'''
if text.count(old) != 1:
    raise SystemExit(f"offsetof index declarations mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''    index_expression = minic_c0_program_expression(parser->program, index_id);
    if (index_expression == NULL || !minic_type_is_integer(index_expression->type)) {
        minic_parser_error(parser, "__builtin_offsetof array index requires an integer");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RBRACKET) {
'''
new = '''    index_expression = minic_c0_program_expression(parser->program, index_id);
    if (index_expression == NULL || !minic_type_is_integer(index_expression->type)) {
        minic_parser_error(parser, "__builtin_offsetof array index requires an integer");
        return false;
    }
    /* The expression pool may grow while adding the stride/scaled nodes below.
     * Snapshot semantic data before any append instead of retaining a pool pointer. */
    index_type = index_expression->type;
    index_span = index_expression->span;
    if (parser->current.kind != MINIC_TOKEN_RBRACKET) {
'''
if text.count(old) != 1:
    raise SystemExit(f"offsetof index snapshot anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''    if (!minic_parser_add_expression(parser, &stride, &stride_id) ||
        !minic_type_integer_common(index_expression->type, stride.type, &scaled_type)) {
'''
new = '''    if (!minic_parser_add_expression(parser, &stride, &stride_id) ||
        !minic_type_integer_common(index_type, stride.type, &scaled_type)) {
'''
if text.count(old) != 1:
    raise SystemExit(f"offsetof index type use mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''    scaled.span.begin = index_expression->span.begin;
'''
new = '''    scaled.span.begin = index_span.begin;
'''
if text.count(old) != 1:
    raise SystemExit(f"offsetof index span use mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
path.write_text(text)
