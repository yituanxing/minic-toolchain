#!/usr/bin/env python3
"""Diagnostic-only census of repeated lexer tokenization from exact source offsets.

Replaces the externally linked lexer function with an identical wrapper around
an unmodified body. Counters are gated by MINIC_LEXER_RESCAN_PROFILE=1.
A byte vector keyed by (source_pointer, pre-scan cursor) measures *actual*
duplicate tokenization in speculative probes versus ordinary parser traversal.
Retains all token logic and diagnostics. Never promote this instrumentation.
"""
from pathlib import Path

p=Path("src/frontend/lexer.c")
src=p.read_text()
old='#include <string.h>'
if src.count(old)!=1:raise SystemExit("MINIC_LEXER_RESCAN_CENSUS=FAIL include anchor")
src=src.replace(old,old+"\n#include <stdlib.h>",1)
old="bool minic_lexer_next(MinicLexer *lexer, MinicToken *token, MinicDiagnostic *diagnostic) {"
if src.count(old)!=1:raise SystemExit("MINIC_LEXER_RESCAN_CENSUS=FAIL function anchor")
src=src.replace(old,"static bool minic_lexer_next_census_implementation(MinicLexer *lexer, MinicToken *token, MinicDiagnostic *diagnostic) {",1)
src += r"""
/* All counters below are CI-only. The actual Lexer implementation above is
 * byte-for-byte the previous lexer_next body. */
typedef struct MinicLexerRescanCensus {
    const char *source;
    size_t source_length;
    unsigned char *seen;
    size_t calls;
    size_t unique_starts;
    size_t repeated_starts;
    size_t third_or_later;
    size_t first_bytes;
    size_t repeated_bytes;
    size_t repeated_identifiers;
    size_t max_visits_same_start;
    size_t untracked_calls;
} MinicLexerRescanCensus;

static MinicLexerRescanCensus minic_lexer_rescan_census;

static void minic_lexer_rescan_census_print(void) {
    const MinicLexerRescanCensus *c = &minic_lexer_rescan_census;
    (void)fprintf(stderr,
                  "MINIC_LEXER_RESCAN_CENSUS=PASS source_bytes=%zu "
                  "calls=%zu unique_starts=%zu repeated_starts=%zu "
                  "third_plus=%zu original_scan_bytes=%zu repeated_scan_bytes=%zu "
                  "repeated_identifier_tokens=%zu max_visits_same_start=%zu "
                  "untracked_calls=%zu\n",
                  c->source_length, c->calls, c->unique_starts,
                  c->repeated_starts, c->third_or_later,
                  c->first_bytes, c->repeated_bytes,
                  c->repeated_identifiers, c->max_visits_same_start,
                  c->untracked_calls);
    free(minic_lexer_rescan_census.seen);
    minic_lexer_rescan_census.seen = NULL;
}

bool minic_lexer_next(MinicLexer *lexer, MinicToken *token, MinicDiagnostic *diagnostic) {
    static int enabled = -1;
    MinicLexerRescanCensus *c = &minic_lexer_rescan_census;
    size_t before = 0U;
    unsigned char previously_seen = 0U;
    bool track = false;
    bool success;

    if (enabled == -1) {
        const char *opt = getenv("MINIC_LEXER_RESCAN_PROFILE");
        enabled = opt != NULL && opt[0] != '\0' && opt[0] != '0' ? 1 : 0;
    }
    if (enabled == 0) {
        return minic_lexer_next_census_implementation(lexer, token, diagnostic);
    }
    if (lexer != NULL && lexer->source != NULL && c->source == NULL &&
        lexer->length != SIZE_MAX) {
        /* One .i per process. Extra origins are reported, not conflated.
         * Memory scales as one byte per input byte; opt-in diagnostics only. */
        unsigned char *seen = (unsigned char *)calloc(lexer->length + 1U, 1U);
        if (seen != NULL) {
            c->source = lexer->source;
            c->source_length = lexer->length;
            c->seen = seen;
            if (atexit(minic_lexer_rescan_census_print) != 0) {
                free(c->seen);
                c->seen = NULL;
                c->source = NULL;
            }
        }
    }
    if (lexer != NULL && c->seen != NULL &&
        lexer->source == c->source &&
        lexer->length == c->source_length &&
        lexer->cursor <= c->source_length) {
        track = true;
        before = lexer->cursor;
        previously_seen = c->seen[before];
        if (previously_seen < 255U) {
            c->seen[before] = (unsigned char)(previously_seen + 1U);
        }
    } else {
        c->untracked_calls += 1U;
    }
    success = minic_lexer_next_census_implementation(lexer, token, diagnostic);
    if (track && lexer->cursor >= before) {
        size_t bytes = lexer->cursor - before;
        c->calls += 1U;
        if (previously_seen == 0U) {
            c->unique_starts += 1U;
            c->first_bytes += bytes;
        } else {
            c->repeated_starts += 1U;
            c->repeated_bytes += bytes;
            if (previously_seen >= 2U) {
                c->third_or_later += 1U;
            }
            if (success && token != NULL && token->kind == MINIC_TOKEN_IDENTIFIER) {
                c->repeated_identifiers += 1U;
            }
        }
        if ((size_t)previously_seen + 1U > c->max_visits_same_start) {
            c->max_visits_same_start = (size_t)previously_seen + 1U;
        }
    }
    return success;
}
"""
p.write_text(src)
print("MINIC_LEXER_RESCAN_CENSUS_PATCH=APPLIED diagnostic_only=true")
