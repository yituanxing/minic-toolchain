#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]

# Give enum syntax/semantic binding a real owner instead of keeping it inside typedef parsing.
(root / "src/frontend/parser_enum.c").write_text(r'''#include "frontend/parser_internal.h"

#include <limits.h>
#include <stdlib.h>

bool minic_parser_find_enum_tag(const MinicParser *parser, MinicSourceSpan name_span) {
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
        resized = (MinicParserEnumTag *)realloc(parser->enum_tags,
                                                new_capacity * sizeof(*parser->enum_tags));
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

bool minic_parser_find_enum_constant(const MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     int *value) {
    size_t index;

    if (parser == NULL) {
        return false;
    }
    for (index = parser->enum_constant_count; index > 0U; --index) {
        const MinicParserEnumConstant *constant = &parser->enum_constants[index - 1U];

        if (minic_parser_span_equals(parser, name_span, constant->name_span)) {
            if (value != NULL) {
                *value = constant->value;
            }
            return true;
        }
    }
    return false;
}

bool minic_parser_bind_enum_constant(MinicParser *parser, MinicSourceSpan name_span, int value) {
    MinicParserEnumConstant *resized;
    size_t new_capacity;

    if (parser == NULL || minic_parser_find_enum_constant(parser, name_span, NULL)) {
        if (parser != NULL) {
            minic_parser_error(parser, "duplicate enumerator name");
        }
        return false;
    }
    if (parser->enum_constant_count == parser->enum_constant_capacity) {
        new_capacity =
            parser->enum_constant_capacity == 0U ? 16U : parser->enum_constant_capacity * 2U;
        if (new_capacity < parser->enum_constant_capacity ||
            new_capacity > SIZE_MAX / sizeof(*parser->enum_constants)) {
            minic_parser_error(parser, "too many enum constants");
            return false;
        }
        resized = (MinicParserEnumConstant *)realloc(
            parser->enum_constants, new_capacity * sizeof(*parser->enum_constants));
        if (resized == NULL) {
            minic_parser_error(parser, "out of memory while binding enum constant");
            return false;
        }
        parser->enum_constants = resized;
        parser->enum_constant_capacity = new_capacity;
    }
    parser->enum_constants[parser->enum_constant_count].name_span = name_span;
    parser->enum_constants[parser->enum_constant_count].value = value;
    parser->enum_constant_count += 1U;
    return true;
}

void minic_parser_destroy_enum_constants(MinicParser *parser) {
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

static bool parse_enum_integer_value(MinicParser *parser, int *value) {
    int64_t parsed;

    if (parser == NULL || value == NULL ||
        !minic_parser_parse_integer_constant_expression(parser, &parsed)) {
        return false;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        minic_parser_error(parser, "enum constant expression is out of int range");
        return false;
    }
    *value = (int)parsed;
    return true;
}

bool minic_parser_parse_enum_specifier(MinicParser *parser, MinicType *enum_type) {
    MinicSourceSpan tag_span;
    int next_value;
    bool has_tag;

    if (parser == NULL || enum_type == NULL ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_ENUM, "expected keyword 'enum'")) {
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

    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (!has_tag || !minic_parser_find_enum_tag(parser, tag_span)) {
            minic_parser_error(parser, has_tag ? "unknown enum tag" : "expected enum tag or definition");
            return false;
        }
        *enum_type = minic_type_int();
        return true;
    }

    if (!minic_parser_advance(parser) || (has_tag && !minic_parser_bind_enum_tag(parser, tag_span))) {
        return false;
    }
    next_value = 0;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicSourceSpan name_span;
        int value;

        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected enumerator name");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        value = next_value;
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            if (!minic_parser_advance(parser) || !parse_enum_integer_value(parser, &value)) {
                return false;
            }
        }
        if (!minic_parser_bind_enum_constant(parser, name_span, value)) {
            return false;
        }
        next_value = value == INT_MAX ? INT_MAX : value + 1;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' after enumerator");
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after enum definition")) {
        return false;
    }
    *enum_type = minic_type_int();
    return true;
}

bool minic_parser_parse_enum_definition(MinicParser *parser) {
    MinicType enum_type;

    return minic_parser_parse_enum_specifier(parser, &enum_type) &&
           minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after enum definition");
}
''')

# Remove enum ownership and the obsolete record-definition probe from typedef.c.
path = root / "src/frontend/parser_typedef.c"
text = path.read_text()
start = text.find("bool minic_parser_find_enum_tag(")
end = text.find("static bool parse_function_pointer_typedef(", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate enum/typedef ownership block")
text = text[:start] + text[end:]
old = '''    bool is_enum_definition;
    bool is_function_pointer;
    bool is_record_definition;

    bound_count = 0U;
    is_function_pointer = false;
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_TYPEDEF, "expected keyword 'typedef'")) {
        return false;
    }
    is_enum_definition = parser->current.kind == MINIC_TOKEN_KW_ENUM;
    if (!typedef_starts_record_definition(parser, &is_record_definition)) {
        return false;
    }
    if (is_enum_definition) {
        if (!parse_enum_definition_specifier(parser)) {
            return false;
        }
        aliased_type = minic_type_int();
    } else if (is_record_definition) {
        if (!minic_parser_parse_record_definition_specifier(parser, &aliased_type)) {
            return false;
        }
    } else {
        MinicType base_type;

        if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
            !minic_parser_parse_pointer_declarator(parser, base_type, &aliased_type)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            if (!parse_function_pointer_typedef(parser, aliased_type, &name_span, &aliased_type)) {
                return false;
            }
            is_function_pointer = true;
        }
    }
'''
new = '''    bool is_function_pointer;

    bound_count = 0U;
    is_function_pointer = false;
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_TYPEDEF, "expected keyword 'typedef'")) {
        return false;
    }
    {
        MinicType base_type;

        if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
            !minic_parser_parse_pointer_declarator(parser, base_type, &aliased_type)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            if (!parse_function_pointer_typedef(parser, aliased_type, &name_span, &aliased_type)) {
                return false;
            }
            is_function_pointer = true;
        }
    }
'''
text = replace_once(text, old, new, "typedef-shared-type-specifier")
path.write_text(text)

# Make enum definitions and references a normal type-specifier consumer.
path = root / "src/frontend/parser_type.c"
text = path.read_text()
old = '''    } else if (parser->current.kind == MINIC_TOKEN_KW_ENUM) {
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
'''
new = '''    } else if (parser->current.kind == MINIC_TOKEN_KW_ENUM) {
        if (!minic_parser_parse_enum_specifier(parser, &parsed_type)) {
            return false;
        }
'''
text = replace_once(text, old, new, "type-shared-enum-specifier")
path.write_text(text)

path = root / "src/frontend/parser_internal.h"
text = path.read_text()
text = replace_once(
    text,
    '''bool minic_parser_bind_enum_tag(MinicParser *parser, MinicSourceSpan name_span);
bool minic_parser_find_enum_tag(const MinicParser *parser, MinicSourceSpan name_span);
''',
    '''bool minic_parser_bind_enum_tag(MinicParser *parser, MinicSourceSpan name_span);
bool minic_parser_find_enum_tag(const MinicParser *parser, MinicSourceSpan name_span);
bool minic_parser_parse_enum_specifier(MinicParser *parser, MinicType *enum_type);
''',
    "enum-specifier-prototype",
)
path.write_text(text)

path = root / "Makefile"
text = path.read_text()
text = replace_once(
    text,
    '''\tsrc/frontend/parser_declarator.c \\
\tsrc/frontend/parser_core.c \\
''',
    '''\tsrc/frontend/parser_declarator.c \\
\tsrc/frontend/parser_enum.c \\
\tsrc/frontend/parser_core.c \\
''',
    "makefile-parser-enum",
)
path.write_text(text)

# Extend the existing enum regression with the exact Linux declaration shape and typedef reference.
path = root / "tests/compiler/c0/enum_tag_type_references.c"
text = path.read_text()
text = replace_once(
    text,
    '''enum lockdep_like {
    LOCKDEP_LIKE_OK,
    LOCKDEP_LIKE_BAD,
};

''',
    '''enum lockdep_like {
    LOCKDEP_LIKE_OK,
    LOCKDEP_LIKE_BAD,
};

extern enum system_states_like {
    SYSTEM_BOOTING_LIKE,
    SYSTEM_RUNNING_LIKE,
} system_state_like;

typedef enum system_states_like system_state_alias;

''',
    "enum-linux-shape-fixture",
)
text = replace_once(
    text,
    '''int main(void) {
    return normalize_state(LOCKDEP_LIKE_OK);
}
''',
    '''int main(void) {
    system_state_alias state = SYSTEM_BOOTING_LIKE;

    return normalize_state(LOCKDEP_LIKE_OK) + state;
}
''',
    "enum-typedef-reference-fixture",
)
path.write_text(text)

path = root / "tests/compiler/c0/run-enum-tag-type-references.sh"
text = path.read_text()
text = replace_once(
    text,
    """printf '%s\\n' 'PASS compiler/c0/enum_tag_type_references definition=tagged reference=parameter+return top-level-bare-return=1 representation=int unknown-tag=reject-by-registry'\n""",
    """grep -F 'system_state_like' "$assembly" >/dev/null || true
printf '%s\\n' 'PASS compiler/c0/enum_tag_type_references definition=tagged+extern-declspec reference=parameter+return+typedef top-level-bare-return=1 representation=int shared-specifier=1 unknown-tag=reject-by-registry'\n""",
    "enum-runner-message",
)
path.write_text(text)
