#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


# Keep enum tags distinct from enumerator constants. The current semantic model
# represents all supported enum values as int, but a tag reference must still
# name a tag that was actually declared/defined in this translation unit.
replace_once(
    "src/frontend/parser_internal.h",
    """typedef struct MinicParserEnumConstant {
    MinicSourceSpan name_span;
    int value;
} MinicParserEnumConstant;
""",
    """typedef struct MinicParserEnumConstant {
    MinicSourceSpan name_span;
    int value;
} MinicParserEnumConstant;

typedef struct MinicParserEnumTag {
    MinicSourceSpan name_span;
} MinicParserEnumTag;
""",
    "enum-tag-struct",
)
replace_once(
    "src/frontend/parser_internal.h",
    """    MinicParserEnumConstant *enum_constants;
    size_t enum_constant_count;
    size_t enum_constant_capacity;
""",
    """    MinicParserEnumConstant *enum_constants;
    size_t enum_constant_count;
    size_t enum_constant_capacity;

    MinicParserEnumTag *enum_tags;
    size_t enum_tag_count;
    size_t enum_tag_capacity;
""",
    "enum-tag-storage",
)
replace_once(
    "src/frontend/parser_internal.h",
    """bool minic_parser_bind_enum_constant(MinicParser *parser, MinicSourceSpan name_span, int value);
bool minic_parser_find_enum_constant(const MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     int *value);
""",
    """bool minic_parser_bind_enum_constant(MinicParser *parser, MinicSourceSpan name_span, int value);
bool minic_parser_find_enum_constant(const MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     int *value);
bool minic_parser_bind_enum_tag(MinicParser *parser, MinicSourceSpan name_span);
bool minic_parser_find_enum_tag(const MinicParser *parser, MinicSourceSpan name_span);
""",
    "enum-tag-prototypes",
)

# Parser-owned tag registry. Source spans are stable for the translation unit,
# so no string copy is needed during discovery; StringId interning can absorb
# this later when the staged frontend is materialized.
path = Path("src/frontend/parser_typedef.c")
text = path.read_text()
marker = "bool minic_parser_find_enum_constant(const MinicParser *parser,\n"
helper = r'''bool minic_parser_find_enum_tag(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return false;
    }
    for (index = parser->enum_tag_count; index > 0U; --index) {
        if (minic_parser_span_equals(parser, name_span, parser->enum_tags[index - 1U].name_span)) {
            return true;
        }
    }
    return false;
}

bool minic_parser_bind_enum_tag(MinicParser *parser, MinicSourceSpan name_span) {
    MinicParserEnumTag *resized;
    size_t new_capacity;

    if (parser == NULL || minic_parser_find_enum_tag(parser, name_span)) {
        if (parser != NULL) {
            minic_parser_error(parser, "duplicate enum tag");
        }
        return false;
    }
    if (parser->enum_tag_count == parser->enum_tag_capacity) {
        new_capacity = parser->enum_tag_capacity == 0U ? 8U : parser->enum_tag_capacity * 2U;
        if (new_capacity < parser->enum_tag_capacity ||
            new_capacity > SIZE_MAX / sizeof(*parser->enum_tags)) {
            minic_parser_error(parser, "too many enum tags");
            return false;
        }
        resized = (MinicParserEnumTag *)realloc(
            parser->enum_tags, new_capacity * sizeof(*parser->enum_tags));
        if (resized == NULL) {
            minic_parser_error(parser, "out of memory while binding enum tag");
            return false;
        }
        parser->enum_tags = resized;
        parser->enum_tag_capacity = new_capacity;
    }
    parser->enum_tags[parser->enum_tag_count].name_span = name_span;
    parser->enum_tag_count += 1U;
    return true;
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"enum tag helper marker: expected one, found {text.count(marker)}")
text = text.replace(marker, helper + marker, 1)

old = r'''void minic_parser_destroy_enum_constants(MinicParser *parser) {
    if (parser == NULL) {
        return;
    }
    free(parser->enum_constants);
    parser->enum_constants = NULL;
    parser->enum_constant_count = 0U;
    parser->enum_constant_capacity = 0U;
}
'''
new = r'''void minic_parser_destroy_enum_constants(MinicParser *parser) {
    if (parser == NULL) {
        return;
    }
    free(parser->enum_constants);
    parser->enum_constants = NULL;
    parser->enum_constant_count = 0U;
    parser->enum_constant_capacity = 0U;
    free(parser->enum_tags);
    parser->enum_tags = NULL;
    parser->enum_tag_count = 0U;
    parser->enum_tag_capacity = 0U;
}
'''
if text.count(old) != 1:
    raise SystemExit(f"enum tag cleanup: expected one destroy function, found {text.count(old)}")
text = text.replace(old, new, 1)

old = r'''static bool parse_enum_definition_specifier(MinicParser *parser) {
    int next_value;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_ENUM, "expected keyword 'enum'")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' after enum specifier")) {
        return false;
    }
'''
new = r'''static bool parse_enum_definition_specifier(MinicParser *parser) {
    MinicSourceSpan tag_span;
    int next_value;
    bool has_tag;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_ENUM, "expected keyword 'enum'")) {
        return false;
    }
    (void)memset(&tag_span, 0, sizeof(tag_span));
    has_tag = false;
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        tag_span = parser->current.span;
        has_tag = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' after enum specifier") ||
        (has_tag && !minic_parser_bind_enum_tag(parser, tag_span))) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"enum definition tag binding: expected one staged definition head, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))

# Type-name lookahead must recognize `enum` anywhere a type can start.
replace_once(
    "src/frontend/parser_type.c",
    """    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
        return true;
""",
    """    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_ENUM:
    case MINIC_TOKEN_KW_STRUCT:
        return true;
""",
    "enum-type-lookahead",
)

# A referenced tag resolves to the compiler's current enum representation (int),
# but only after validating that the tag is known. This preserves type-name
# legality without pretending the compiler already models arbitrary enum
# underlying types.
path = Path("src/frontend/parser_type.c")
text = path.read_text()
anchor = """    } else if (parser->current.kind == MINIC_TOKEN_KW_STRUCT) {
        MinicRecordId record_id;
"""
arm = r'''    } else if (parser->current.kind == MINIC_TOKEN_KW_ENUM) {
        MinicSourceSpan tag_span;

        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected enum tag after 'enum'");
            return false;
        }
        tag_span = parser->current.span;
        if (!minic_parser_find_enum_tag(parser, tag_span)) {
            minic_parser_error(parser, "unknown enum tag");
            return false;
        }
        parsed_type = minic_type_int();
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_KW_STRUCT) {
        MinicRecordId record_id;
'''
if text.count(anchor) != 1:
    raise SystemExit(f"enum type parser arm: expected one struct arm, found {text.count(anchor)}")
path.write_text(text.replace(anchor, arm, 1))

print("staged enum tag registry and validated enum-tag type-name references")
