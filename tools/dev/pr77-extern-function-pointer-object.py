#!/usr/bin/env python3
from pathlib import Path


def function_region(text: str, signature: str) -> tuple[int, int, str]:
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"extern function pointer object: missing {signature}")
    end_candidates = [
        pos
        for pos in (
            text.find("\nstatic ", start + len(signature)),
            text.find("\nbool ", start + len(signature)),
            text.find("\nvoid ", start + len(signature)),
        )
        if pos >= 0
    ]
    end = min(end_candidates) if end_candidates else len(text)
    return start, end, text[start:end]


path = Path("src/frontend/parser_global.c")
text = path.read_text()
start, end, body = function_region(text, "bool minic_parser_parse_extern_global(MinicParser *parser) {")

# Install an object-declarator helper immediately before parse_extern_global.
helper = r'''static bool parse_extern_function_pointer_object_declarator(MinicParser *parser,
                                                            MinicType return_type,
                                                            MinicSourceSpan *name_span,
                                                            MinicType *object_type) {
    MinicType parameter_types[8];
    MinicType function_type;
    size_t parameter_count;
    size_t pointer_depth;
    bool is_variadic;

    if (parser == NULL || name_span == NULL || object_type == NULL ||
        parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }
    parameter_count = 0U;
    pointer_depth = 0U;
    is_variadic = false;
    (void)memset(parameter_types, 0, sizeof(parameter_types));

    if (!minic_parser_advance(parser)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STAR) {
        pointer_depth += 1U;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (pointer_depth == 0U) {
        minic_parser_error(parser, "extern parenthesized object declarator requires '*'");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected extern function pointer object name");
        return false;
    }
    *name_span = parser->current.span;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser,
                            MINIC_TOKEN_RPAREN,
                            "expected ')' after extern function pointer object name") ||
        !minic_parser_expect(parser,
                            MINIC_TOKEN_LPAREN,
                            "expected '(' before extern function pointer parameters") ||
        !minic_parser_parse_parameter_list(
            parser, NULL, parameter_types, &parameter_count, false, &is_variadic) ||
        !minic_parser_expect(parser,
                            MINIC_TOKEN_RPAREN,
                            "expected ')' after extern function pointer parameters")) {
        return false;
    }
    if (is_variadic) {
        minic_parser_error(parser, "variadic extern function pointer objects are not supported yet");
        return false;
    }
    if (!minic_c0_program_add_function_type(
            parser->program, return_type, parameter_types, parameter_count, &function_type)) {
        minic_parser_error(parser, "cannot build extern function pointer object type");
        return false;
    }
    while (pointer_depth > 0U) {
        if (!minic_type_pointer_to(function_type, &function_type)) {
            minic_parser_error(parser, "extern function pointer object depth is unsupported");
            return false;
        }
        pointer_depth -= 1U;
    }
    *object_type = function_type;
    return true;
}

'''
text = text[:start] + helper + text[start:]
path.write_text(text)

# Re-read after helper insertion and replace only the declarator/name portion of
# the staged extern-object parser. Type legality is checked after the declarator
# so `extern void (*fp)(void)` remains legal while `extern void object;` remains
# rejected.
text = path.read_text()
start, end, body = function_region(text, "bool minic_parser_parse_extern_global(MinicParser *parser) {")
old = r'''    if (minic_type_is_void(object_type) || minic_type_is_function(object_type) ||
        minic_type_is_array(object_type)) {
        minic_parser_error(parser, "unsupported extern object type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected extern object name");
        return false;
    }
    name_span = parser->current.span;
    if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
        minic_parser_error(parser, "duplicate global object");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }
'''
new = r'''    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!parse_extern_function_pointer_object_declarator(
                parser, object_type, &name_span, &object_type)) {
            return false;
        }
    } else {
        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected extern object name");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (minic_type_is_void(object_type) || minic_type_is_function(object_type) ||
        minic_type_is_array(object_type)) {
        minic_parser_error(parser, "unsupported extern object type");
        return false;
    }
    if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
        minic_parser_error(parser, "duplicate global object");
        return false;
    }
'''
if body.count(old) != 1:
    raise SystemExit(f"extern function pointer object: expected one staged object declarator block, found {body.count(old)}")
body = body.replace(old, new, 1)
path.write_text(text[:start] + body + text[end:])

print("staged extern function pointer object declarators with owned function types")
