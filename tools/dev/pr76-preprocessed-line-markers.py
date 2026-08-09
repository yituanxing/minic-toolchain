#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/lexer.c")
text = path.read_text()

anchor = '''static bool minic_is_decimal_digit(char character) {\n'''
helper = r'''static bool minic_lexer_skip_gcc_line_marker(MinicLexer *lexer) {
    size_t probe;

    if (lexer == NULL || minic_lexer_peek(lexer) != '#' || lexer->column != 1U) {
        return false;
    }

    probe = lexer->cursor + 1U;
    while (probe < lexer->length &&
           (lexer->source[probe] == ' ' || lexer->source[probe] == '\t')) {
        probe += 1U;
    }
    if (probe >= lexer->length ||
        lexer->source[probe] < '0' || lexer->source[probe] > '9') {
        return false;
    }

    /* GCC -E emits numeric line-control markers in preprocessed .i files, e.g.
     * `# 1 "header.h" 1 3 4`. They are preprocessing metadata, not C tokens.
     * Only numeric markers at the beginning of a physical line are consumed;
     * arbitrary preprocessor directives remain errors instead of being hidden. */
    while (minic_lexer_peek(lexer) != '\0' && minic_lexer_peek(lexer) != '\n') {
        minic_lexer_advance(lexer);
    }
    if (minic_lexer_peek(lexer) == '\n') {
        minic_lexer_advance(lexer);
    }
    return true;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"line-marker helper anchor: expected 1 match, found {text.count(anchor)}")
text = text.replace(anchor, helper + anchor, 1)

old = '''    while (minic_is_space(minic_lexer_peek(lexer))) {
        minic_lexer_advance(lexer);
    }

    begin = minic_lexer_position(lexer);
'''
new = '''    for (;;) {
        while (minic_is_space(minic_lexer_peek(lexer))) {
            minic_lexer_advance(lexer);
        }
        if (!minic_lexer_skip_gcc_line_marker(lexer)) {
            break;
        }
    }

    begin = minic_lexer_position(lexer);
'''
if text.count(old) != 1:
    raise SystemExit(f"line-marker scan anchor: expected 1 match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged GCC numeric line markers in preprocessed .i input")
