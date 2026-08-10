#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_function.c"
text = path.read_text()

anchor = '''static bool parse_function(MinicParser *parser, bool is_internal) {
'''
helper = r'''typedef struct MinicParsedDeclarationPrefix {
    MinicParsedAttributeList attributes;
    bool is_extern;
    bool is_static;
    bool is_inline;
} MinicParsedDeclarationPrefix;

static bool parse_declaration_prefix(MinicParser *parser,
                                     bool require_initial_static,
                                     MinicParsedDeclarationPrefix *prefix) {
    bool saw_storage_class;

    if (parser == NULL || prefix == NULL) {
        return false;
    }
    (void)memset(prefix, 0, sizeof(*prefix));
    saw_storage_class = false;

    for (;;) {
        if (parser->current.kind == MINIC_TOKEN_KW_STATIC) {
            if (saw_storage_class) {
                minic_parser_error(parser, "conflicting or duplicate declaration storage class");
                return false;
            }
            prefix->is_static = true;
            saw_storage_class = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_KW_EXTERN) {
            if (saw_storage_class) {
                minic_parser_error(parser, "conflicting or duplicate declaration storage class");
                return false;
            }
            prefix->is_extern = true;
            saw_storage_class = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_KW_INLINE) {
            if (prefix->is_inline) {
                minic_parser_error(parser, "duplicate inline declaration specifier");
                return false;
            }
            prefix->is_inline = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (function_identifier_is(parser, "__attribute__")) {
            size_t old_offset = parser->current.span.begin.offset;

            if (!minic_parser_collect_gnu_attribute_lists(parser, &prefix->attributes)) {
                return false;
            }
            if (parser->current.span.begin.offset == old_offset) {
                minic_parser_error(parser, "internal error: GNU attribute prefix made no progress");
                return false;
            }
            continue;
        }
        break;
    }

    if (require_initial_static && !prefix->is_static) {
        minic_parser_error(parser, "expected keyword 'static'");
        return false;
    }
    return true;
}

'''
text = replace_once(text, anchor, helper + anchor, "declaration-prefix-helper")

old_vars = '''    MinicParsedAttributeList deferred_attributes;
    MinicBlockId body_block;
'''
new_vars = '''    MinicParsedAttributeList deferred_attributes;
    MinicParsedDeclarationPrefix declaration_prefix;
    MinicBlockId body_block;
'''
text = replace_once(text, old_vars, new_vars, "declaration-prefix-state")

old_init = '''    is_extern_declaration = false;
    is_function_pointer_object = false;
    is_inline = false;
    is_static_declaration = is_internal;
    (void)memset(&deferred_attributes, 0, sizeof(deferred_attributes));
'''
new_init = '''    is_extern_declaration = false;
    is_function_pointer_object = false;
    is_inline = false;
    is_static_declaration = false;
    (void)memset(&deferred_attributes, 0, sizeof(deferred_attributes));
    (void)memset(&declaration_prefix, 0, sizeof(declaration_prefix));
'''
text = replace_once(text, old_init, new_init, "declaration-prefix-init")

old_prefix = '''    if (!parse_gnu_prefix_function_visibility(parser, &visibility, &has_visibility)) {
        return false;
    }
    if (is_internal &&
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'")) {
        return false;
    }
    if (!is_internal && parser->current.kind == MINIC_TOKEN_KW_EXTERN) {
        is_extern_declaration = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (parser->current.kind == MINIC_TOKEN_KW_INLINE) {
        is_inline = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!is_internal && parser->current.kind == MINIC_TOKEN_KW_STATIC) {
        is_internal = true;
        is_static_declaration = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_parser_collect_gnu_attribute_lists(parser, &deferred_attributes) ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
'''
new_prefix = '''    if (!parse_gnu_prefix_function_visibility(parser, &visibility, &has_visibility) ||
        !parse_declaration_prefix(parser, is_internal, &declaration_prefix)) {
        return false;
    }
    is_extern_declaration = declaration_prefix.is_extern;
    is_static_declaration = declaration_prefix.is_static;
    is_internal = declaration_prefix.is_static;
    is_inline = declaration_prefix.is_inline;
    deferred_attributes = declaration_prefix.attributes;
    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
'''
text = replace_once(text, old_prefix, new_prefix, "declaration-prefix-use")
path.write_text(text)

path = root / "tests/compiler/c0/gnu_format_cold_attributes.c"
text = path.read_text()
text = replace_once(
    text,
    '''static int probe(void) {
    return 0;
}

''',
    '''static __attribute__((__format__(printf, 1, 0))) inline __attribute__((__gnu_inline__))
    __attribute__((__unused__)) __attribute__((__no_instrument_function__)) int
ftrace_vprintk_like(const char *fmt, void *ap) {
    (void)fmt;
    (void)ap;
    return 0;
}

inline static __attribute__((__unused__)) int reordered_static_inline(void) {
    return 0;
}

static int probe(void) {
    return ftrace_vprintk_like("x", (void *)0) + reordered_static_inline();
}

''',
    "format-interleaved-fixture",
)
path.write_text(text)

path = root / "tests/compiler/c0/run-gnu-format-cold-attributes.sh"
text = path.read_text()
text = replace_once(
    text,
    '''grep -F 'main:' "$assembly" >/dev/null
printf '%s\\n' 'PASS compiler/c0/gnu_format_cold_attributes prefix=format suffix=noreturn,cold classification=diagnostic+optimization ABI=unchanged'
''',
    '''grep -F 'main:' "$assembly" >/dev/null
grep -F 'ftrace_vprintk_like:' "$assembly" >/dev/null
grep -F 'reordered_static_inline:' "$assembly" >/dev/null
printf '%s\\n' 'PASS compiler/c0/gnu_format_cold_attributes prefix=format suffix=noreturn,cold interleaved=static-attr-inline-attr+inline-static classification=diagnostic+optimization ABI=unchanged'
''',
    "format-interleaved-runner",
)
path.write_text(text)
