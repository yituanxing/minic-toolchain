#!/usr/bin/env python3
"""Opt-in one-factor lexer identifier run advance, diagnostic A/B only.

Replace per-identifier-byte minic_lexer_advance() calls with one cursor
and column update.  Identifier ASCII cannot contain newlines; token spans,
keyword classification, embedded NUL stop, and rejection remain unchanged.
Do NOT alter production lexer.c until real TU A/B and all safety gates.
"""
from pathlib import Path

p=Path("src/frontend/lexer.c")
source=p.read_text()
old="""    if (minic_is_identifier_start(character)) {
        size_t start;

        start = lexer->cursor;
        do {
            minic_lexer_advance(lexer);
        } while (minic_is_identifier_continue(minic_lexer_peek(lexer)));
        token->kind = minic_classify_identifier(lexer->source + start, lexer->cursor - start);
        token->span.end = minic_lexer_position(lexer);
        return true;
    }
"""
new="""    if (minic_is_identifier_start(character)) {
        size_t start = lexer->cursor;
        size_t end = start + 1U;

        /* Identifiers contain ASCII letters, '_' and digits only: no
         * newline is possible. Avoid a bounds check and line/column
         * branch for each byte; preserve the exact source-token span. */
        while (end < lexer->length &&
               minic_is_identifier_continue(lexer->source[end])) {
            ++end;
        }
        lexer->cursor = end;
        lexer->column += end - start;
        token->kind = minic_classify_identifier(lexer->source + start, end - start);
        token->span.end = minic_lexer_position(lexer);
        return true;
    }
"""
if source.count(old)!=1:
    raise SystemExit("MINIC_LEXER_IDENTIFIER_BULK_PATCH=FAIL anchor drift")
if source.count("static void minic_lexer_advance(")!=1:
    raise SystemExit("MINIC_LEXER_IDENTIFIER_BULK_PATCH=FAIL lexer advance contract changed")
p.write_text(source.replace(old,new,1))
print("MINIC_LEXER_IDENTIFIER_BULK_PATCH=APPLIED opt_in_only=true")
