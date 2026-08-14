#!/usr/bin/env python3
from pathlib import Path

# parser_internal.h: parser-side visible tag bindings and scope checkpoint.
path = Path('src/frontend/parser_internal.h')
text = path.read_text()
old = '''typedef struct MinicParserLocalBinding {
    MinicSourceSpan name_span;
    MinicLocalId local_id;
    MinicGlobalObjectId global_object_id;
} MinicParserLocalBinding;

typedef struct MinicParserScopeFrame {
    size_t binding_begin;
    MinicCleanupContextId cleanup_context;
} MinicParserScopeFrame;
'''
new = '''typedef struct MinicParserLocalBinding {
    MinicSourceSpan name_span;
    MinicLocalId local_id;
    MinicGlobalObjectId global_object_id;
} MinicParserLocalBinding;

typedef struct MinicParserRecordTag {
    MinicSourceSpan name_span;
    MinicRecordId record_id;
} MinicParserRecordTag;

typedef struct MinicParserScopeFrame {
    size_t binding_begin;
    size_t record_tag_begin;
    MinicCleanupContextId cleanup_context;
} MinicParserScopeFrame;
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = '''    MinicParserLocalBinding *local_bindings;
    size_t local_binding_count;
    size_t local_binding_capacity;

    MinicParserScopeFrame *scopes;
'''
new = '''    MinicParserLocalBinding *local_bindings;
    size_t local_binding_count;
    size_t local_binding_capacity;

    MinicParserRecordTag *record_tags;
    size_t record_tag_count;
    size_t record_tag_capacity;

    MinicParserScopeFrame *scopes;
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = '''MinicRecordId minic_parser_find_record(const MinicParser *parser, MinicSourceSpan name_span);
MinicTypeAliasId minic_parser_find_type_alias'''
new = '''MinicRecordId minic_parser_find_record(const MinicParser *parser, MinicSourceSpan name_span);
MinicRecordId minic_parser_find_record_in_current_scope(const MinicParser *parser,
                                                        MinicSourceSpan name_span);
bool minic_parser_bind_record_tag(MinicParser *parser,
                                  MinicSourceSpan name_span,
                                  MinicRecordId record_id);
MinicTypeAliasId minic_parser_find_type_alias'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
path.write_text(text)

# parser_core.c: visible tag namespace is parser state; Program owns entities only.
path = Path('src/frontend/parser_core.c')
text = path.read_text()
old = '''    scope = &parser->scopes[parser->scope_count];
    scope->binding_begin = parser->local_binding_count;
    scope->cleanup_context = parser->cleanup_context;
'''
new = '''    scope = &parser->scopes[parser->scope_count];
    scope->binding_begin = parser->local_binding_count;
    scope->record_tag_begin = parser->record_tag_count;
    scope->cleanup_context = parser->cleanup_context;
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = '''    parser->scope_count -= 1U;
    parser->local_binding_count = parser->scopes[parser->scope_count].binding_begin;
    parser->cleanup_context = parser->scopes[parser->scope_count].cleanup_context;
'''
new = '''    parser->scope_count -= 1U;
    parser->local_binding_count = parser->scopes[parser->scope_count].binding_begin;
    parser->record_tag_count = parser->scopes[parser->scope_count].record_tag_begin;
    parser->cleanup_context = parser->scopes[parser->scope_count].cleanup_context;
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = '''    free(parser->local_bindings);
    free(parser->scopes);
    parser->local_bindings = NULL;
'''
new = '''    free(parser->local_bindings);
    free(parser->record_tags);
    free(parser->scopes);
    parser->local_bindings = NULL;
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = '''    parser->local_binding_capacity = 0U;
    parser->scopes = NULL;
'''
new = '''    parser->local_binding_capacity = 0U;
    parser->record_tags = NULL;
    parser->record_tag_count = 0U;
    parser->record_tag_capacity = 0U;
    parser->scopes = NULL;
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = '''MinicRecordId minic_parser_find_record(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->record_count; ++index) {
        const MinicRecord *record;

        record = minic_c0_program_record(parser->program, index);
        if (record != NULL && record->name_length == name_length &&
            memcmp(record->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
    }
    return MINIC_RECORD_INVALID;
}
'''
new = '''MinicRecordId minic_parser_find_record(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return MINIC_RECORD_INVALID;
    }
    for (index = parser->record_tag_count; index > 0U; --index) {
        const MinicParserRecordTag *tag;

        tag = &parser->record_tags[index - 1U];
        if (minic_parser_span_equals(parser, tag->name_span, name_span)) {
            return tag->record_id;
        }
    }
    return MINIC_RECORD_INVALID;
}

MinicRecordId minic_parser_find_record_in_current_scope(const MinicParser *parser,
                                                        MinicSourceSpan name_span) {
    size_t begin;
    size_t index;

    if (parser == NULL) {
        return MINIC_RECORD_INVALID;
    }
    begin = parser->scope_count == 0U ? 0U : parser->scopes[parser->scope_count - 1U].record_tag_begin;
    for (index = parser->record_tag_count; index > begin; --index) {
        const MinicParserRecordTag *tag;

        tag = &parser->record_tags[index - 1U];
        if (minic_parser_span_equals(parser, tag->name_span, name_span)) {
            return tag->record_id;
        }
    }
    return MINIC_RECORD_INVALID;
}

bool minic_parser_bind_record_tag(MinicParser *parser,
                                  MinicSourceSpan name_span,
                                  MinicRecordId record_id) {
    MinicParserRecordTag *tag;

    if (parser == NULL || record_id == MINIC_RECORD_INVALID ||
        minic_parser_find_record_in_current_scope(parser, name_span) != MINIC_RECORD_INVALID) {
        if (parser != NULL) {
            minic_parser_error(parser, "duplicate record tag binding in current scope");
        }
        return false;
    }
    if (parser->record_tag_count == parser->record_tag_capacity &&
        !minic_parser_grow_array((void **)&parser->record_tags,
                                 &parser->record_tag_capacity,
                                 sizeof(*parser->record_tags))) {
        minic_parser_error(parser, "out of memory while binding record tag");
        return false;
    }
    tag = &parser->record_tags[parser->record_tag_count];
    tag->name_span = name_span;
    tag->record_id = record_id;
    parser->record_tag_count += 1U;
    return true;
}
'''
assert text.count(old) == 1
path.write_text(text.replace(old, new, 1))

# parser_record.c: definitions and standalone forwards introduce/reuse current-scope tags.
path = Path('src/frontend/parser_record.c')
text = path.read_text()
old = '''        name_span = parser->current.span;
        record_id = minic_parser_find_record(parser, name_span);
        if (record_id == MINIC_RECORD_INVALID) {
            if (!minic_c0_program_add_record(parser->program,
                                             parser->source + name_span.begin.offset,
                                             minic_parser_span_length(name_span),
                                             &record_id)) {
                minic_parser_error(parser, "out of memory while adding record");
                return false;
            }
            parser->program->records[record_id].is_union = is_union;
        } else {
'''
new = '''        name_span = parser->current.span;
        record_id = minic_parser_find_record_in_current_scope(parser, name_span);
        if (record_id == MINIC_RECORD_INVALID) {
            if (!minic_c0_program_add_record(parser->program,
                                             parser->source + name_span.begin.offset,
                                             minic_parser_span_length(name_span),
                                             &record_id) ||
                !minic_parser_bind_record_tag(parser, name_span, record_id)) {
                minic_parser_error(parser, "out of memory while adding record");
                return false;
            }
            parser->program->records[record_id].is_union = is_union;
        } else {
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = '''bool minic_parser_parse_record_definition(MinicParser *parser) {
    MinicParser probe;
    MinicType record_type;
    bool is_forward_declaration;

    if (parser == NULL) {
        return false;
    }

    probe = *parser;
    is_forward_declaration = false;
    if (minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_IDENTIFIER &&
        minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_SEMICOLON) {
        is_forward_declaration = true;
    }

    if (is_forward_declaration) {
        return minic_parser_parse_type_specifiers(parser, &record_type) &&
               minic_type_is_record(record_type) &&
               minic_parser_expect(
                   parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record declaration");
    }

    return minic_parser_parse_record_definition_specifier(parser, &record_type) &&
           minic_parser_expect(
               parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record definition");
}
'''
new = '''static bool parse_record_forward_declaration(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicRecordId record_id;
    MinicTokenKind keyword;
    bool is_union;

    keyword = parser->current.kind;
    is_union = keyword == MINIC_TOKEN_KW_UNION;
    if ((keyword != MINIC_TOKEN_KW_STRUCT && keyword != MINIC_TOKEN_KW_UNION) ||
        !minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record tag in forward declaration");
        return false;
    }
    name_span = parser->current.span;
    record_id = minic_parser_find_record_in_current_scope(parser, name_span);
    if (record_id == MINIC_RECORD_INVALID) {
        if (!minic_c0_program_add_record(parser->program,
                                         parser->source + name_span.begin.offset,
                                         minic_parser_span_length(name_span),
                                         &record_id) ||
            !minic_parser_bind_record_tag(parser, name_span, record_id)) {
            minic_parser_error(parser, "cannot create forward record tag");
            return false;
        }
        parser->program->records[record_id].is_union = is_union;
    } else if (parser->program->records[record_id].is_union != is_union) {
        minic_parser_error(parser, "record tag kind does not match prior declaration");
        return false;
    }
    return minic_parser_advance(parser) &&
           minic_parser_expect(parser,
                               MINIC_TOKEN_SEMICOLON,
                               "expected ';' after record declaration");
}

bool minic_parser_parse_record_definition(MinicParser *parser) {
    MinicParser probe;
    MinicType record_type;

    if (parser == NULL) {
        return false;
    }

    probe = *parser;
    if (minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_IDENTIFIER &&
        minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_SEMICOLON) {
        return parse_record_forward_declaration(parser);
    }

    return minic_parser_parse_record_definition_specifier(parser, &record_type) &&
           minic_parser_expect(
               parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record definition");
}
'''
assert text.count(old) == 1
path.write_text(text.replace(old, new, 1))

# parser_type.c: ordinary references use nearest visible tag; new incomplete tags bind current scope.
path = Path('src/frontend/parser_type.c')
text = path.read_text()
old = '''            if (record_id == MINIC_RECORD_INVALID) {
                if (!minic_c0_program_add_record(parser->program,
                                                 parser->source + parser->current.span.begin.offset,
                                                 minic_parser_span_length(parser->current.span),
                                                 &record_id)) {
                    minic_parser_error(parser, "out of memory while declaring record tag");
                    return false;
                }
                parser->program->records[record_id].is_union = is_union;
'''
new = '''            if (record_id == MINIC_RECORD_INVALID) {
                const MinicSourceSpan name_span = parser->current.span;

                if (!minic_c0_program_add_record(parser->program,
                                                 parser->source + name_span.begin.offset,
                                                 minic_parser_span_length(name_span),
                                                 &record_id) ||
                    !minic_parser_bind_record_tag(parser, name_span, record_id)) {
                    minic_parser_error(parser, "out of memory while declaring record tag");
                    return false;
                }
                parser->program->records[record_id].is_union = is_union;
'''
assert text.count(old) == 1
path.write_text(text.replace(old, new, 1))

# parser_statement.c: identify true standalone block record declarations without stealing
# `struct S { ... } object;` from the normal local-declarator path.
path = Path('src/frontend/parser_statement.c')
text = path.read_text()
anchor = '''static bool token_starts_local_declaration(const MinicParser *parser) {
    return parser != NULL &&
           minic_parser_token_starts_declaration_specifiers(parser, parser->current);
}

bool minic_parser_parse_statement(MinicParser *parser, bool allow_declaration) {
'''
replacement = '''static bool token_starts_local_declaration(const MinicParser *parser) {
    return parser != NULL &&
           minic_parser_token_starts_declaration_specifiers(parser, parser->current);
}

static bool block_record_starts_standalone_declaration(MinicParser *parser, bool *is_standalone) {
    MinicParser probe;
    size_t brace_depth;

    if (parser == NULL || is_standalone == NULL ||
        (parser->current.kind != MINIC_TOKEN_KW_STRUCT &&
         parser->current.kind != MINIC_TOKEN_KW_UNION)) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_IDENTIFIER && !minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_SEMICOLON) {
        *is_standalone = true;
        return true;
    }
    if (probe.current.kind != MINIC_TOKEN_LBRACE) {
        *is_standalone = false;
        return true;
    }

    brace_depth = 0U;
    do {
        if (probe.current.kind == MINIC_TOKEN_LBRACE) {
            brace_depth += 1U;
        } else if (probe.current.kind == MINIC_TOKEN_RBRACE) {
            if (brace_depth == 0U) {
                return false;
            }
            brace_depth -= 1U;
        }
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    } while (brace_depth != 0U);
    *is_standalone = probe.current.kind == MINIC_TOKEN_SEMICOLON;
    return true;
}

bool minic_parser_parse_statement(MinicParser *parser, bool allow_declaration) {
'''
assert text.count(anchor) == 1
text = text.replace(anchor, replacement, 1)
old = '''    if (token_starts_local_declaration(parser)) {
        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        return parse_declaration(parser);
    }
'''
new = '''    if (parser->current.kind == MINIC_TOKEN_KW_STRUCT ||
        parser->current.kind == MINIC_TOKEN_KW_UNION) {
        bool is_standalone;

        if (!block_record_starts_standalone_declaration(parser, &is_standalone)) {
            return false;
        }
        if (is_standalone) {
            if (!allow_declaration) {
                minic_parser_error(parser, "a declaration requires a compound statement scope");
                return false;
            }
            return minic_parser_parse_record_definition(parser);
        }
    }
    if (token_starts_local_declaration(parser)) {
        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        return parse_declaration(parser);
    }
'''
assert text.count(old) == 1
path.write_text(text.replace(old, new, 1))

# Focused positive source.
Path('tests/compiler/c0/block_scope_record_tags.c').write_text(r'''struct shadow_tag {
    int outer;
};

int block_scope_record_tags(void) {
    struct shadow_tag before;
    before.outer = 1;

    {
        struct shadow_tag {
            long inner;
        };
        struct shadow_tag inner_value;
        inner_value.inner = 2;
        if (inner_value.inner != 2)
            return 1;
    }

    {
        struct same_scope;
        struct same_scope {
            int value;
        };
        struct same_scope local_value;
        local_value.value = 3;
        if (local_value.value != 3)
            return 2;
    }

    union cpumask_rcuhead {
        int cpumask;
        long rcu;
    };
    union cpumask_rcuhead mask;
    mask.cpumask = 4;
    if (mask.cpumask != 4)
        return 3;

    {
        struct with_declarator {
            int value;
        } object;
        object.value = 5;
        if (object.value != 5)
            return 4;
    }

    {
        struct shadow_tag after;
        after.outer = 6;
        if (after.outer != 6)
            return 5;
    }
    return before.outer == 1 ? 0 : 6;
}

int main(void) {
    return block_scope_record_tags();
}
''')

# Focused runner, including a negative forward-shadow check.
Path('tests/compiler/c0/run-block-scope-record-tags.sh').write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-block-scope-record-tags

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/block_scope_record_tags.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'block_scope_record_tags:' "$work/output.s" >/dev/null
grep -F 'main:' "$work/output.s" >/dev/null

cat >"$work/forward-shadow.c" <<'EOF'
struct shadow_tag { int outer; };
int forward_shadow(void) {
    {
        struct shadow_tag;
        return sizeof(struct shadow_tag);
    }
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/forward-shadow.c" -o "$work/forward-shadow.i"
if "$minic" -S "$work/forward-shadow.i" -o "$work/forward-shadow.s" \
    >"$work/forward-shadow.out" 2>"$work/forward-shadow.err"; then
    printf '%s\n' 'FAIL compiler/c0/block-scope-record-tags: block forward tag did not shadow outer complete tag' >&2
    exit 1
fi
grep -F 'incomplete' "$work/forward-shadow.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/block-scope-record-tags standalone=struct+union shadow=nested restore=outer forward=current-scope definition=reuse definition-with-declarator=preserved'
''')

# Put the new contract into the permanent Foundation focused owner.
path = Path('tests/compiler/c0/run-foundation-focused.sh')
text = path.read_text()
anchor = '''    run-anonymous-record-members.sh \\
    run-block-scope-extern-function-attributes.sh \\
'''
replacement = '''    run-anonymous-record-members.sh \\
    run-block-scope-record-tags.sh \\
    run-block-scope-extern-function-attributes.sh \\
'''
assert text.count(anchor) == 1
path.write_text(text.replace(anchor, replacement, 1))
