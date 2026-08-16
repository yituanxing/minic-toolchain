from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_attribute.c",
    """typedef struct MinicObjectAttributeContext {
    char *section_name;
    size_t section_capacity;
    size_t *section_name_length;
    bool *has_section;
    size_t *explicit_alignment;
} MinicObjectAttributeContext;
""",
    """typedef struct MinicObjectAttributeContext {
    char *section_name;
    size_t section_capacity;
    size_t *section_name_length;
    bool *has_section;
    size_t *explicit_alignment;
    MinicSymbolVisibility *visibility;
    bool *has_visibility;
} MinicObjectAttributeContext;
""",
)

replace_once(
    "src/frontend/parser_attribute.c",
    """static bool object_attribute_class_is_parse_only(MinicAttributeClass semantic_class) {
""",
    """static bool parse_object_visibility_argument(MinicParser *parser,
                                             const MinicParsedAttribute *attribute,
                                             MinicSymbolVisibility *visibility) {
    size_t cursor;
    size_t end;
    size_t value_begin;
    size_t value_length;
    const char *value;

    if (parser == NULL || attribute == NULL || visibility == NULL || !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    cursor = attribute->arguments_span.begin.offset + 1U;
    end = attribute->arguments_span.end.offset - 1U;
    while (cursor < end && (parser->source[cursor] == ' ' || parser->source[cursor] == '\\t' ||
                            parser->source[cursor] == '\\n' || parser->source[cursor] == '\\r' ||
                            parser->source[cursor] == '\\f' || parser->source[cursor] == '\\v')) {
        cursor += 1U;
    }
    if (cursor >= end || parser->source[cursor] != '\"') {
        minic_parser_error(parser, "GNU visibility attribute requires one string literal");
        return false;
    }
    cursor += 1U;
    value_begin = cursor;
    while (cursor < end && parser->source[cursor] != '\"') {
        if (parser->source[cursor] == '\\\\') {
            minic_parser_error(parser, "escaped GNU visibility values are not supported yet");
            return false;
        }
        cursor += 1U;
    }
    if (cursor >= end || parser->source[cursor] != '\"') {
        minic_parser_error(parser, "unterminated GNU visibility value");
        return false;
    }
    value_length = cursor - value_begin;
    value = parser->source + value_begin;
    cursor += 1U;
    while (cursor < end && (parser->source[cursor] == ' ' || parser->source[cursor] == '\\t' ||
                            parser->source[cursor] == '\\n' || parser->source[cursor] == '\\r' ||
                            parser->source[cursor] == '\\f' || parser->source[cursor] == '\\v')) {
        cursor += 1U;
    }
    if (cursor != end) {
        minic_parser_error(parser, "GNU visibility attribute requires exactly one string literal");
        return false;
    }
    if (value_length == 8U && memcmp(value, "internal", 8U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_INTERNAL;
    } else if (value_length == 6U && memcmp(value, "hidden", 6U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_HIDDEN;
    } else if (value_length == 9U && memcmp(value, "protected", 9U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_PROTECTED;
    } else if (value_length == 7U && memcmp(value, "default", 7U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    } else {
        minic_parser_error(parser, "unsupported GNU visibility value");
        return false;
    }
    return true;
}

static bool apply_object_visibility_attribute(MinicParser *parser,
                                              const MinicParsedAttribute *attribute,
                                              MinicSymbolVisibility *visibility,
                                              bool *has_visibility) {
    MinicSymbolVisibility parsed_visibility;

    if (parser == NULL || attribute == NULL || visibility == NULL || has_visibility == NULL ||
        attribute->descriptor == NULL || attribute->descriptor->kind != MINIC_ATTRIBUTE_VISIBILITY ||
        !parse_object_visibility_argument(parser, attribute, &parsed_visibility)) {
        return false;
    }
    if (*has_visibility && *visibility != parsed_visibility) {
        minic_parser_error(parser, "conflicting GNU object visibility attributes");
        return false;
    }
    *visibility = parsed_visibility;
    *has_visibility = true;
    return true;
}

static bool object_attribute_class_is_parse_only(MinicAttributeClass semantic_class) {
""",
)

replace_once(
    "src/frontend/parser_attribute.c",
    """    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "object", context->explicit_alignment);
    }
    minic_parser_error(parser,
                       "unsupported GNU object attribute; symbol/layout attributes require "
                       "explicit object semantics");
""",
    """    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "object", context->explicit_alignment);
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_VISIBILITY && context->visibility != NULL &&
        context->has_visibility != NULL) {
        return apply_object_visibility_attribute(
            parser, attribute, context->visibility, context->has_visibility);
    }
    minic_parser_error(parser,
                       "unsupported GNU object attribute; symbol/layout attributes require "
                       "explicit object semantics");
""",
)

replace_once(
    "src/frontend/parser_attribute.c",
    """    context->has_section = has_section;
    context->explicit_alignment = explicit_alignment;
    return true;
}
""",
    """    context->has_section = has_section;
    context->explicit_alignment = explicit_alignment;
    context->visibility = NULL;
    context->has_visibility = NULL;
    return true;
}
""",
)

replace_once(
    "src/frontend/parser_attribute.c",
    """bool minic_parser_apply_alignment_attribute(MinicParser *parser,
""",
    """bool minic_parser_parse_gnu_object_attribute_lists_with_visibility(
    MinicParser *parser,
    char *section_name,
    size_t section_capacity,
    size_t *section_name_length,
    bool *has_section,
    size_t *explicit_alignment,
    MinicSymbolVisibility *visibility,
    bool *has_visibility) {
    MinicObjectAttributeContext context;

    if (parser == NULL || visibility == NULL || has_visibility == NULL ||
        !initialize_object_attribute_context(&context,
                                             section_name,
                                             section_capacity,
                                             section_name_length,
                                             has_section,
                                             explicit_alignment)) {
        return false;
    }
    context.visibility = visibility;
    context.has_visibility = has_visibility;
    return minic_parser_parse_gnu_attribute_lists(parser, consume_object_attribute, &context);
}

bool minic_parser_apply_alignment_attribute(MinicParser *parser,
""",
)

replace_once(
    "src/frontend/parser_internal.h",
    """bool minic_parser_parse_gnu_object_attribute_lists(MinicParser *parser,
                                                   char *section_name,
                                                   size_t section_capacity,
                                                   size_t *section_name_length,
                                                   bool *has_section,
                                                   size_t *explicit_alignment);
""",
    """bool minic_parser_parse_gnu_object_attribute_lists(MinicParser *parser,
                                                   char *section_name,
                                                   size_t section_capacity,
                                                   size_t *section_name_length,
                                                   bool *has_section,
                                                   size_t *explicit_alignment);
bool minic_parser_parse_gnu_object_attribute_lists_with_visibility(
    MinicParser *parser,
    char *section_name,
    size_t section_capacity,
    size_t *section_name_length,
    bool *has_section,
    size_t *explicit_alignment,
    MinicSymbolVisibility *visibility,
    bool *has_visibility);
""",
)

path = Path("src/frontend/parser_global.c")
text = path.read_text()
start = text.index("bool minic_parser_parse_extern_global_after_head(")
end = text.index("\nbool minic_parser_parse_extern_global(MinicParser *parser)", start)
chunk = text[start:end]

old = """        size_t declarator_explicit_alignment;
        bool declarator_has_section;
        bool is_array;
"""
new = """        size_t declarator_explicit_alignment;
        bool declarator_has_section;
        MinicSymbolVisibility declarator_visibility;
        bool declarator_has_visibility;
        bool is_array;
"""
if chunk.count(old) != 1:
    raise SystemExit(f"parser_global declarations anchor={chunk.count(old)}")
chunk = chunk.replace(old, new, 1)

old = """        declarator_explicit_alignment = shared_explicit_alignment;
        declarator_has_section = has_section;
        (void)memset(declarator_section_name, 0, sizeof(declarator_section_name));
"""
new = """        declarator_explicit_alignment = shared_explicit_alignment;
        declarator_has_section = has_section;
        declarator_visibility = visibility;
        declarator_has_visibility = has_visibility;
        (void)memset(declarator_section_name, 0, sizeof(declarator_section_name));
"""
if chunk.count(old) != 1:
    raise SystemExit(f"parser_global initialization anchor={chunk.count(old)}")
chunk = chunk.replace(old, new, 1)

old = """minic_parser_parse_gnu_object_attribute_lists(parser,
                                                           declarator_section_name,
                                                           sizeof(declarator_section_name),
                                                           &declarator_section_name_length,
                                                           &declarator_has_section,
                                                           &declarator_explicit_alignment)"""
new = """minic_parser_parse_gnu_object_attribute_lists_with_visibility(
                parser,
                declarator_section_name,
                sizeof(declarator_section_name),
                &declarator_section_name_length,
                &declarator_has_section,
                &declarator_explicit_alignment,
                &declarator_visibility,
                &declarator_has_visibility)"""
if chunk.count(old) != 2:
    raise SystemExit(f"parser_global object-list calls={chunk.count(old)}")
chunk = chunk.replace(old, new)

old = """                                                 declarator_explicit_alignment,
                                                 visibility,
                                                 has_visibility)) {
"""
new = """                                                 declarator_explicit_alignment,
                                                 declarator_visibility,
                                                 declarator_has_visibility)) {
"""
if chunk.count(old) != 1:
    raise SystemExit(f"parser_global merge visibility anchor={chunk.count(old)}")
chunk = chunk.replace(old, new, 1)

old = """                   (has_visibility && !minic_c0_global_object_set_visibility(
                                          parser->program, object_id, visibility))) {
"""
new = """                   (declarator_has_visibility &&
                    !minic_c0_global_object_set_visibility(
                        parser->program, object_id, declarator_visibility))) {
"""
if chunk.count(old) != 1:
    raise SystemExit(f"parser_global new visibility anchor={chunk.count(old)}")
chunk = chunk.replace(old, new, 1)
path.write_text(text[:start] + chunk + text[end:])

source = Path("tests/compiler/c0/gnu_visible_extern_array.c")
source.write_text(
    source.read_text().rstrip()
    + """

struct hidden_item {
    int value;
};

extern struct hidden_item hidden_items[2] __attribute__((visibility("hidden")));
struct hidden_item hidden_items[2] = {{1}, {2}};
"""
)

runner = Path("tests/compiler/c0/run-gnu-visible-extern-arrays.sh")
text = runner.read_text()
old = """grep -F '.size names, 24' "$work/gnu_visible_extern_array.s" >/dev/null

test "$(grep -c '^  .dword .Lminic_string_' "$work/gnu_visible_extern_array.s")" -ge 3
printf '%s\\n' 'PASS compiler/c0/gnu_visible_extern_array declaration=fixed-pointer-array definition=merge visibility=internal size=24'
"""
new = """grep -F '.size names, 24' "$work/gnu_visible_extern_array.s" >/dev/null
grep -F '.globl hidden_items' "$work/gnu_visible_extern_array.s" >/dev/null
grep -F '.hidden hidden_items' "$work/gnu_visible_extern_array.s" >/dev/null
grep -F '.size hidden_items, 8' "$work/gnu_visible_extern_array.s" >/dev/null

test "$(grep -c '^  .dword .Lminic_string_' "$work/gnu_visible_extern_array.s")" -ge 3
printf '%s\\n' 'PASS compiler/c0/gnu_visible_extern_array prefix=internal suffix=hidden extern-array=merge size=24,8'
"""
if text.count(old) != 1:
    raise SystemExit(f"visible extern array runner anchor={text.count(old)}")
runner.write_text(text.replace(old, new, 1))
