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
    """static bool object_attribute_visibility_value(const MinicParser *parser,
                                              MinicSourceSpan span,
                                              MinicSymbolVisibility *visibility) {
    const char *value;
    size_t length;

    if (parser == NULL || visibility == NULL || span.end.offset <= span.begin.offset + 1U ||
        parser->source[span.begin.offset] != '\"' || parser->source[span.end.offset - 1U] != '\"') {
        return false;
    }
    value = parser->source + span.begin.offset + 1U;
    length = span.end.offset - span.begin.offset - 2U;
    if (length == 8U && memcmp(value, "internal", 8U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_INTERNAL;
    } else if (length == 6U && memcmp(value, "hidden", 6U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_HIDDEN;
    } else if (length == 9U && memcmp(value, "protected", 9U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_PROTECTED;
    } else if (length == 7U && memcmp(value, "default", 7U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    } else {
        return false;
    }
    return true;
}

static bool apply_object_visibility_attribute(MinicParser *parser,
                                              const MinicParsedAttribute *attribute,
                                              MinicSymbolVisibility *visibility,
                                              bool *has_visibility) {
    MinicParser probe;
    MinicSymbolVisibility parsed_visibility;

    if (parser == NULL || attribute == NULL || visibility == NULL || has_visibility == NULL ||
        attribute->descriptor == NULL || attribute->descriptor->kind != MINIC_ATTRIBUTE_VISIBILITY ||
        !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    probe = *parser;
    probe.lexer.cursor = attribute->arguments_span.begin.offset + 1U;
    if (!minic_lexer_next(&probe.lexer, &probe.current) ||
        probe.current.kind != MINIC_TOKEN_STRING_LITERAL ||
        !object_attribute_visibility_value(&probe, probe.current.span, &parsed_visibility) ||
        !minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_RPAREN ||
        probe.current.span.end.offset != attribute->arguments_span.end.offset) {
        minic_parser_error(parser, "GNU visibility attribute requires one supported string literal");
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
    if (!object_attribute_class_is_parse_only(descriptor->semantic_class)) {
""",
    """    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "object", context->explicit_alignment);
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_VISIBILITY) {
        if (context->visibility == NULL || context->has_visibility == NULL ||
            !apply_object_visibility_attribute(
                parser, attribute, context->visibility, context->has_visibility)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "%s", context->unsupported_message);
            }
            return false;
        }
        return true;
    }
    if (!object_attribute_class_is_parse_only(descriptor->semantic_class)) {
""",
)

replace_once(
    "src/frontend/parser_attribute.c",
    """    context->explicit_alignment = explicit_alignment;
    context->unsupported_message =
""",
    """    context->explicit_alignment = explicit_alignment;
    context->visibility = NULL;
    context->has_visibility = NULL;
    context->unsupported_message =
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

    if (visibility == NULL || has_visibility == NULL ||
        !initialize_object_attribute_context(parser,
                                             &context,
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

replace_once(
    "src/frontend/parser_global.c",
    """        size_t declarator_explicit_alignment;
        bool declarator_has_section;
        bool is_array;
""",
    """        size_t declarator_explicit_alignment;
        bool declarator_has_section;
        MinicSymbolVisibility declarator_visibility;
        bool declarator_has_visibility;
        bool is_array;
""",
)
replace_once(
    "src/frontend/parser_global.c",
    """        declarator_explicit_alignment = shared_explicit_alignment;
        declarator_has_section = has_section;
        (void)memset(declarator_section_name, 0, sizeof(declarator_section_name));
""",
    """        declarator_explicit_alignment = shared_explicit_alignment;
        declarator_has_section = has_section;
        declarator_visibility = visibility;
        declarator_has_visibility = has_visibility;
        (void)memset(declarator_section_name, 0, sizeof(declarator_section_name));
""",
)

old_call = """        if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                           declarator_section_name,
                                                           sizeof(declarator_section_name),
                                                           &declarator_section_name_length,
                                                           &declarator_has_section,
                                                           &declarator_explicit_alignment)) {
"""
new_call = """        if (!minic_parser_parse_gnu_object_attribute_lists_with_visibility(
                parser,
                declarator_section_name,
                sizeof(declarator_section_name),
                &declarator_section_name_length,
                &declarator_has_section,
                &declarator_explicit_alignment,
                &declarator_visibility,
                &declarator_has_visibility)) {
"""
path = Path("src/frontend/parser_global.c")
text = path.read_text()
if text.count(old_call) < 1:
    raise SystemExit("parser_global.c: first extern object attribute anchor missing")
text = text.replace(old_call, new_call, 1)
old_second = """            !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                           declarator_section_name,
                                                           sizeof(declarator_section_name),
                                                           &declarator_section_name_length,
                                                           &declarator_has_section,
                                                           &declarator_explicit_alignment)) {
"""
new_second = """            !minic_parser_parse_gnu_object_attribute_lists_with_visibility(
                parser,
                declarator_section_name,
                sizeof(declarator_section_name),
                &declarator_section_name_length,
                &declarator_has_section,
                &declarator_explicit_alignment,
                &declarator_visibility,
                &declarator_has_visibility)) {
"""
if text.count(old_second) < 1:
    raise SystemExit("parser_global.c: post-array object attribute anchor missing")
text = text.replace(old_second, new_second, 1)
text = text.replace(
    """                                                 visibility,
                                                 has_visibility)) {
""",
    """                                                 declarator_visibility,
                                                 declarator_has_visibility)) {
""",
    1,
)
text = text.replace(
    """                   (has_visibility && !minic_c0_global_object_set_visibility(
                                          parser->program, object_id, visibility))) {
""",
    """                   (declarator_has_visibility && !minic_c0_global_object_set_visibility(
                                                       parser->program,
                                                       object_id,
                                                       declarator_visibility))) {
""",
    1,
)
path.write_text(text)

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
replace = """grep -F '.size names, 24' "$work/gnu_visible_extern_array.s" >/dev/null

test "$(grep -c '^  .dword .Lminic_string_' "$work/gnu_visible_extern_array.s")" -ge 3
printf '%s\\n' 'PASS compiler/c0/gnu_visible_extern_array declaration=fixed-pointer-array definition=merge visibility=internal size=24'
"""
with_text = """grep -F '.size names, 24' "$work/gnu_visible_extern_array.s" >/dev/null
grep -F '.globl hidden_items' "$work/gnu_visible_extern_array.s" >/dev/null
grep -F '.hidden hidden_items' "$work/gnu_visible_extern_array.s" >/dev/null
grep -F '.size hidden_items, 8' "$work/gnu_visible_extern_array.s" >/dev/null

test "$(grep -c '^  .dword .Lminic_string_' "$work/gnu_visible_extern_array.s")" -ge 3
printf '%s\\n' 'PASS compiler/c0/gnu_visible_extern_array prefix=internal suffix=hidden extern-array=merge size=24,8'
"""
if text.count(replace) != 1:
    raise SystemExit("visible extern array runner anchor missing")
runner.write_text(text.replace(replace, with_text, 1))
