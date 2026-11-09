#include "frontend/gnu_neon_inline_filter.h"
#include "frontend/lexer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct HeaderInline {
    size_t begin, brace, end, name;
    bool needed;
} HeaderInline;

typedef struct HeaderOption {
    size_t token_index, closing_index, depth;
} HeaderOption;

typedef struct HeaderScan {
    MinicToken *tokens;
    size_t token_count, token_capacity;
    HeaderInline *functions;
    size_t function_count, function_capacity;
    size_t *slots;
    size_t slot_capacity;
    HeaderOption *options;
    size_t option_count, option_capacity;
} HeaderScan;

static bool grow(void **p, size_t *capacity, size_t count, size_t size) {
    size_t next = *capacity != 0U ? *capacity * 2U : 64U;
    void *allocated;
    if (count < *capacity) return true;
    if (next < count + 1U || next > SIZE_MAX / size) return false;
    allocated = realloc(*p, next * size);
    if (allocated == NULL) return false;
    *p = allocated;
    *capacity = next;
    return true;
}

static bool token_is(const char *source, MinicToken token, const char *spelling) {
    size_t size = token.span.end.offset - token.span.begin.offset;
    return strlen(spelling) == size &&
           memcmp(source + token.span.begin.offset, spelling, size) == 0;
}

static bool directive_is(const char *source, MinicToken token, const char *spelling) {
    size_t len = token.span.end.offset - token.span.begin.offset;
    size_t expected = strlen(spelling);
    if (token.kind != MINIC_TOKEN_PREPROCESSOR_DIRECTIVE) return false;
    while (len > 0U &&
           (source[token.span.begin.offset + len - 1U] == '\n' ||
            source[token.span.begin.offset + len - 1U] == '\r' ||
            source[token.span.begin.offset + len - 1U] == ' ' ||
            source[token.span.begin.offset + len - 1U] == '\t')) --len;
    return len == expected &&
           memcmp(source + token.span.begin.offset, spelling, expected) == 0;
}

static const char *const gcc13_option_targets[] = {
        "#pragma GCC target (\"+nothing+rdma\")",
        "#pragma GCC target (\"+nothing+aes\")",
        "#pragma GCC target (\"+nothing+sha2\")",
        "#pragma GCC target (\"+nothing+fp16\")",
        "#pragma GCC target (\"+nothing+simd+fp16\")",
        "#pragma GCC target (\"+nothing+dotprod\")",
        "#pragma GCC target (\"+nothing+sm4\")",
        "#pragma GCC target (\"+nothing+sha3\")",
        "#pragma GCC target (\"arch=armv8.3-a\")",
        "#pragma GCC target (\"+fp16\")",
        "#pragma GCC target (\"+nothing+fp16fml\")",
        "#pragma GCC target (\"arch=armv8.5-a\")",
        "#pragma GCC target (\"+nothing+bf16+nosimd\")",
        "#pragma GCC target (\"+nothing+bf16\")",
        "#pragma GCC target (\"+nothing+i8mm\")"
};

static bool optional_gnu_header_target(const char *source, MinicToken token) {
    size_t i;
    for (i = 0U; i < sizeof(gcc13_option_targets) /
                         sizeof(gcc13_option_targets[0]); ++i)
        if (directive_is(source, token, gcc13_option_targets[i])) return true;
    return false;
}

static bool parse_tokens(HeaderScan *scan, const char *path,
                         const char *source, size_t length) {
    MinicLexer lexer;
    MinicToken token;
    MinicDiagnostic diagnostic = {0};
    minic_lexer_initialize(&lexer, path, source, length);
    for (;;) {
        if (!minic_lexer_next(&lexer, &token, &diagnostic)) return false;
        if (!grow((void **)&scan->tokens, &scan->token_capacity,
                  scan->token_count, sizeof(*scan->tokens))) return false;
        scan->tokens[scan->token_count++] = token;
        if (token.kind == MINIC_TOKEN_EOF) break;
    }
    return true;
}

static bool header_candidate(const HeaderScan *scan, const char *source,
                             size_t first, size_t brace, size_t *name) {
    bool extension = false, storage = false, inline_kw = false;
    bool attr = false, always = false, gnu = false, artificial = false;
    size_t k, depth = 0U, left_paren = SIZE_MAX;
    if (first >= brace || brace - first > 180U) return false;
    for (k = first; k < brace; ++k) {
        const MinicToken t = scan->tokens[k];
        if (t.kind == MINIC_TOKEN_IDENTIFIER) {
            extension |= token_is(source, t, "__extension__");
            attr |= token_is(source, t, "__attribute__") ||
                    token_is(source, t, "__attribute");
            always |= token_is(source, t, "__always_inline__");
            gnu |= token_is(source, t, "__gnu_inline__");
            artificial |= token_is(source, t, "__artificial__");
            /* Constructors, used/alias and other retained entry points
             * must never have their source bodies erased. */
            if (token_is(source, t, "__used__") ||
                token_is(source, t, "used") ||
                token_is(source, t, "__constructor__") ||
                token_is(source, t, "constructor") ||
                token_is(source, t, "__alias__") ||
                token_is(source, t, "alias")) return false;
        }
        storage |= t.kind == MINIC_TOKEN_KW_EXTERN;
        inline_kw |= t.kind == MINIC_TOKEN_KW_INLINE;
    }
    if (!extension || !storage || !inline_kw || !attr ||
        !always || !gnu || !artificial) return false;
    /* For GNU arm_neon.h the trailing group before the body is exactly the
     * C parameter list. Never guess through a trailing attribute/declarator. */
    if (scan->tokens[brace - 1U].kind != MINIC_TOKEN_RPAREN) return false;
    for (k = brace; k > first;) {
        MinicTokenKind kind = scan->tokens[--k].kind;
        if (kind == MINIC_TOKEN_RPAREN) ++depth;
        else if (kind == MINIC_TOKEN_LPAREN) {
            if (depth == 0U) return false;
            if (--depth == 0U) { left_paren = k; break; }
        }
    }
    if (left_paren == SIZE_MAX || left_paren == first ||
        scan->tokens[left_paren - 1U].kind != MINIC_TOKEN_IDENTIFIER)
        return false;
    *name = left_paren - 1U;
    return true;
}

static bool enumerate_header_wrappers(HeaderScan *scan, const char *source) {
    size_t i, begin = 0U, options_depth = 0U;
    bool pending_target = false, active = false;
    bool header_consumed = false, header_closed = false;
    for (i = 0U; i < scan->token_count; ++i) {
        MinicToken token = scan->tokens[i];
        if (token.kind == MINIC_TOKEN_PREPROCESSOR_DIRECTIVE) {
            if (directive_is(source, token, "#pragma GCC push_options")) {
                ++options_depth;
                pending_target = false;
            } else if (directive_is(source, token,
                                  "#pragma GCC target (\"+nothing+simd\")") &&
                       options_depth == 1U) {
                pending_target = true;
            } else if (directive_is(source, token,
                                  "#pragma GCC aarch64 \"arm_neon.h\"") &&
                       pending_target && options_depth == 1U &&
                       !header_consumed) {
                active = true;
                header_consumed = true;
            } else if (active && optional_gnu_header_target(source, token) &&
                       options_depth > 0U) {
                if (!grow((void **)&scan->options, &scan->option_capacity,
                          scan->option_count, sizeof(*scan->options))) return false;
                scan->options[scan->option_count++] =
                    (HeaderOption){i, SIZE_MAX, options_depth};
            } else if (directive_is(source, token, "#pragma GCC pop_options")) {
                size_t oi;
                for (oi = 0U; oi < scan->option_count; ++oi)
                    if (scan->options[oi].depth == options_depth &&
                        scan->options[oi].closing_index == SIZE_MAX)
                        scan->options[oi].closing_index = i;
                if (options_depth > 0U) --options_depth;
                /* GCC 13 arm_neon.h has disjoint top-level FP16/SHA3/BF16
                 * islands and a real final group of poly128 inline functions
                 * AFTER its final pop. A depth-zero pop is not an EOF. */
                pending_target = false;
            }
            begin = i + 1U;
            continue;
        }
        if (token.kind == MINIC_TOKEN_SEMICOLON) {
            begin = i + 1U;
        } else if (token.kind == MINIC_TOKEN_LBRACE) {
            size_t j, depth = 1U, name = SIZE_MAX;
            bool candidate;
            for (j = i + 1U; j < scan->token_count && depth > 0U; ++j) {
                if (scan->tokens[j].kind == MINIC_TOKEN_LBRACE) ++depth;
                else if (scan->tokens[j].kind == MINIC_TOKEN_RBRACE) --depth;
            }
            if (depth != 0U) return false; /* original parser diagnoses it */
            candidate = active && header_candidate(scan, source, begin, i, &name);
            if (candidate) {
                if (!grow((void **)&scan->functions, &scan->function_capacity,
                          scan->function_count, sizeof(*scan->functions))) return false;
                scan->functions[scan->function_count++] =
                    (HeaderInline){begin, i, j - 1U, name, false};
            }
            if (candidate && options_depth == 0U &&
                token_is(source, scan->tokens[name], "vaddq_p128")) {
                /* GCC 13 -E -P discards original #line provenance. Its
                 * verified final arm_neon.h definition is the only remaining
                 * trusted end boundary: after vaddq_p128, never skip a
                 * user-defined lookalike. An unknown GCC version without
                 * this exact terminal wrapper fails closed below. */
                header_closed = true;
                active = false;
            }
            i = j - 1U;
            begin = j;
        }
    }
    if (header_consumed) {
        size_t j;
        /* Hard fail closed for a truncated, rearranged or newer GNU header.
         * GCC 13 -E -P no longer preserves filesystem provenance; both the
         * complete wrapper population and feature-island order are part of
         * this deliberately version-pinned source-only optimization. */
        if (!header_closed || scan->function_count != 3900U ||
            scan->option_count != sizeof(gcc13_option_targets) /
                                  sizeof(gcc13_option_targets[0])) return false;
        for (j = 0U; j < scan->option_count; ++j)
            if (!directive_is(source, scan->tokens[scan->options[j].token_index],
                              gcc13_option_targets[j])) return false;
    }
    return true;
}

static size_t hash_span(const char *source, MinicToken token) {
    size_t i, hash = (size_t)2166136261U;
    for (i = token.span.begin.offset; i < token.span.end.offset; ++i)
        hash = (hash ^ (unsigned char)source[i]) * (size_t)16777619U;
    return hash;
}

static size_t find_wrapper(const HeaderScan *scan, const char *source,
                           MinicToken token) {
    size_t at, start;
    if (token.kind != MINIC_TOKEN_IDENTIFIER || scan->slot_capacity == 0U)
        return SIZE_MAX;
    start = hash_span(source, token) & (scan->slot_capacity - 1U);
    at = start;
    while (scan->slots[at] != 0U) {
        size_t idx = scan->slots[at] - 1U;
        MinicToken name = scan->tokens[scan->functions[idx].name];
        size_t n = name.span.end.offset - name.span.begin.offset;
        if (n == token.span.end.offset - token.span.begin.offset &&
            memcmp(source + name.span.begin.offset,
                   source + token.span.begin.offset, n) == 0) return idx;
        at = (at + 1U) & (scan->slot_capacity - 1U);
        if (at == start) break;
    }
    return SIZE_MAX;
}

static bool index_wrappers(HeaderScan *scan, const char *source) {
    size_t i, capacity = 16U;
    while (capacity < scan->function_count * 2U) {
        if (capacity > SIZE_MAX / 2U) return false;
        capacity *= 2U;
    }
    scan->slots = calloc(capacity, sizeof(*scan->slots));
    if (scan->slots == NULL) return false;
    scan->slot_capacity = capacity;
    for (i = 0U; i < scan->function_count; ++i) {
        MinicToken name = scan->tokens[scan->functions[i].name];
        size_t at = hash_span(source, name) & (capacity - 1U);
        if (find_wrapper(scan, source, name) != SIZE_MAX) return false;
        while (scan->slots[at] != 0U) at = (at + 1U) & (capacity - 1U);
        scan->slots[at] = i + 1U;
    }
    return true;
}

static bool mark_live(HeaderScan *scan, const char *source) {
    size_t i, w = 0U, used = 0U, consumed = 0U;
    size_t *work;
    if (scan->function_count == 0U) return true;
    work = malloc(scan->function_count * sizeof(*work));
    if (work == NULL) return false;
    /* All noncandidate declarations, function bodies and initializers form
     * the conservative root set: taking a function address is also a use. */
    for (i = 0U; i < scan->token_count; ++i) {
        size_t candidate;
        while (w < scan->function_count && i > scan->functions[w].end) ++w;
        if (w < scan->function_count &&
            i >= scan->functions[w].begin && i <= scan->functions[w].end)
            continue;
        candidate = find_wrapper(scan, source, scan->tokens[i]);
        if (candidate != SIZE_MAX && !scan->functions[candidate].needed) {
            scan->functions[candidate].needed = true;
            work[used++] = candidate;
        }
    }
    while (consumed < used) {
        HeaderInline fn = scan->functions[work[consumed++]];
        for (i = fn.begin; i <= fn.end; ++i) {
            size_t candidate = find_wrapper(scan, source, scan->tokens[i]);
            if (candidate != SIZE_MAX && candidate != work[consumed - 1U] &&
                !scan->functions[candidate].needed) {
                scan->functions[candidate].needed = true;
                work[used++] = candidate;
            }
        }
    }
    free(work);
    return true;
}

static bool option_region_is_wrapper_only(const HeaderScan *scan,
                                          const char *source,
                                          HeaderOption option) {
    size_t i, w = 0U;
    if (option.closing_index == SIZE_MAX) return false;
    /* The feature pragma is erased only if its complete lexical scope is
     * attributable to known GNU inline definitions, exact inert typedefs,
     * or the header's own option stack. An unrelated declaration, asm,
     * executable definition or unrecognized directive vetoes erasure. */
    for (i = option.token_index + 1U; i < option.closing_index; ++i) {
        MinicToken token = scan->tokens[i];
        while (w < scan->function_count && i > scan->functions[w].end) ++w;
        if (w < scan->function_count &&
            i >= scan->functions[w].begin && i <= scan->functions[w].end) {
            if (scan->functions[w].needed) return false;
            i = scan->functions[w].end;
            continue;
        }
        if (token.kind == MINIC_TOKEN_PREPROCESSOR_DIRECTIVE &&
            (directive_is(source, token, "#pragma GCC push_options") ||
             directive_is(source, token, "#pragma GCC pop_options") ||
             optional_gnu_header_target(source, token))) continue;
        if (token.kind == MINIC_TOKEN_KW_TYPEDEF &&
            i + 3U < option.closing_index &&
            token_is(source, scan->tokens[i + 1U], "__fp16") &&
            token_is(source, scan->tokens[i + 2U], "float16_t") &&
            scan->tokens[i + 3U].kind == MINIC_TOKEN_SEMICOLON) {
            i += 3U;
            continue;
        }
        return false;
    }
    return true;
}

bool minic_gnu_neon_inline_filter(const char *path, const char *source,
                                  size_t length, const MinicTargetInvocation *target,
                                  char **filtered) {
    HeaderScan scan = {0};
    size_t i, replaced = 0U;
    bool ok;
    static const char marker[] = "#pragma GCC aarch64 \"arm_neon.h\"";
    bool marker_found = false;
    if (filtered == NULL) return false;
    *filtered = NULL;
    if (!minic_target_info_gnu_neon_header_inline_liveness(target)) return true;
    /* Most AArch64 TUs never include this system header. Do not run a second
     * lexer for them merely because the architecture supports the feature. */
    for (i = 0U; i + sizeof(marker) - 1U <= length; ++i) {
        if (source[i] == '#' &&
            memcmp(source + i, marker, sizeof(marker) - 1U) == 0) {
            marker_found = true;
            break;
        }
    }
    if (!marker_found) return true;
    if (!parse_tokens(&scan, path, source, length) ||
        !enumerate_header_wrappers(&scan, source)) {
        free(scan.tokens); free(scan.functions); free(scan.slots); free(scan.options);
        return true; /* fail closed: retain unmodified source and old parser */
    }
    ok = index_wrappers(&scan, source) && mark_live(&scan, source);
    if (getenv("MINIC_GNU_INLINE_FILTER_TRACE") != NULL) {
        size_t live = 0U;
        for (i = 0U; i < scan.function_count; ++i)
            if (scan.functions[i].needed) {
                MinicToken name = scan.tokens[scan.functions[i].name];
                ++live;
                (void)fprintf(stderr, "GNU_INLINE_LIVE %.*s\n",
                              (int)(name.span.end.offset - name.span.begin.offset),
                              source + name.span.begin.offset);
            }
        (void)fprintf(stderr, "GNU_INLINE_FILTER candidates=%zu live=%zu ok=%d tokens=%zu\n",
                      scan.function_count, live, ok ? 1 : 0, scan.token_count);
    }
    if (!ok) {
        free(scan.tokens); free(scan.functions); free(scan.slots); free(scan.options);
        return true; /* ambiguous duplicates, allocation or liveness: no skip */
    }
    for (i = 0U; i < scan.function_count; ++i) {
        HeaderInline fn = scan.functions[i];
        size_t from, to, p;
        if (fn.needed) continue;
        /* An opaque FP16/SVE return type may be legal in a declaration but
         * not a definition. Truly unreferenced wrappers have no C linkage
         * identity to preserve, so erase the whole definition (not just its
         * statements) while retaining every source coordinate. */
        from = scan.tokens[fn.begin].span.begin.offset;
        to = scan.tokens[fn.end].span.end.offset;
        if (*filtered == NULL) {
            *filtered = malloc(length + 1U);
            if (*filtered == NULL) break;
            memcpy(*filtered, source, length);
            (*filtered)[length] = '\0';
        }
        for (p = from; p < to; ++p)
            if ((*filtered)[p] != '\n' && (*filtered)[p] != '\r')
                (*filtered)[p] = ' ';
        ++replaced;
    }
    /* A GCC header feature island may be omitted ONLY when none of its
     * wrappers survive liveness. Preserve all push/pop nesting and source
     * coordinates, and reject every target string outside this exact list.
     * In particular an invoked SHA3/FP16 wrapper retains its original
     * target pragma, so unsupported machine capabilities still fail closed. */
    for (i = 0U; i < scan.option_count; ++i) {
        HeaderOption option = scan.options[i];
        size_t j, from, to, p;
        bool used = false;
        if (option.closing_index == SIZE_MAX) continue;
        for (j = 0U; j < scan.function_count; ++j) {
            HeaderInline fn = scan.functions[j];
            if (fn.needed && fn.begin > option.token_index &&
                fn.end < option.closing_index) { used = true; break; }
        }
        if (used || !option_region_is_wrapper_only(&scan, source, option))
            continue;
        if (*filtered == NULL) {
            *filtered = malloc(length + 1U);
            if (*filtered == NULL) break;
            memcpy(*filtered, source, length);
            (*filtered)[length] = '\0';
        }
        from = scan.tokens[option.token_index].span.begin.offset;
        to = scan.tokens[option.token_index].span.end.offset;
        for (p = from; p < to; ++p)
            if ((*filtered)[p] != '\n' && (*filtered)[p] != '\r')
                (*filtered)[p] = ' ';
    }
    (void)replaced;
    free(scan.tokens); free(scan.functions); free(scan.slots); free(scan.options);
    return true;
}
