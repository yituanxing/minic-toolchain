#!/usr/bin/env python3
from pathlib import Path


path = Path("src/frontend/parser_function.c")
text = path.read_text()
helper_anchor = """static bool static_declaration_is_function(MinicParser *parser, bool *is_function) {
"""
helper = r'''static bool top_level_is_gnu_extension_marker(const MinicParser *parser) {
    static const char marker[] = "__extension__";
    size_t length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return length == sizeof(marker) - 1U &&
           memcmp(parser->source + parser->current.span.begin.offset, marker, length) == 0;
}

static bool skip_top_level_gnu_extension_markers(MinicParser *parser) {
    while (top_level_is_gnu_extension_marker(parser)) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return true;
}

static bool static_declaration_is_function(MinicParser *parser, bool *is_function) {
'''
if text.count(helper_anchor) != 1:
    raise SystemExit(
        f"expected one static declaration probe anchor, found {text.count(helper_anchor)}"
    )
text = text.replace(helper_anchor, helper, 1)

loop_anchor = """    success = minic_parser_advance(&parser);
    while (success && parser.current.kind != MINIC_TOKEN_EOF) {
        if (parser.current.kind == MINIC_TOKEN_KW_TYPEDEF) {
"""
loop_replacement = """    success = minic_parser_advance(&parser);
    while (success && parser.current.kind != MINIC_TOKEN_EOF) {
        success = skip_top_level_gnu_extension_markers(&parser);
        if (!success || parser.current.kind == MINIC_TOKEN_EOF) {
            break;
        }
        if (parser.current.kind == MINIC_TOKEN_KW_TYPEDEF) {
"""
if text.count(loop_anchor) != 1:
    raise SystemExit(f"expected one program dispatch loop anchor, found {text.count(loop_anchor)}")
text = text.replace(loop_anchor, loop_replacement, 1)
path.write_text(text)
print("staged GNU __extension__ as a top-level diagnostic-only declaration marker")
