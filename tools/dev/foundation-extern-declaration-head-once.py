#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def replace_range(text: str, start: str, end: str, new: str, label: str) -> str:
    begin = text.find(start)
    if begin < 0:
        raise SystemExit(f"{label}: start anchor not found")
    finish = text.find(end, begin)
    if finish < 0:
        raise SystemExit(f"{label}: end anchor not found")
    return text[:begin] + new + text[finish:]


root = Path(__file__).resolve().parents[2]

# 1) Parser contract: transient attribute storage + extern after-head consumer.
path = root / "src/frontend/parser_internal.h"
text = path.read_text()
text = replace_once(
    text,
    "#define MINIC_PARSER_MAX_SWITCH_CASES 128U\n",
    "#define MINIC_PARSER_MAX_SWITCH_CASES 128U\n#define MINIC_MAX_PARSED_ATTRIBUTES 32U\n",
    "parsed-attribute-limit",
)
text = replace_once(
    text,
    "typedef struct MinicParsedAttribute {\n"
    "    const MinicAttributeDescriptor *descriptor;\n"
    "    MinicSourceSpan name_span;\n"
    "    MinicSourceSpan arguments_span;\n"
    "    bool has_arguments;\n"
    "} MinicParsedAttribute;\n\n",
    "typedef struct MinicParsedAttribute {\n"
    "    const MinicAttributeDescriptor *descriptor;\n"
    "    MinicSourceSpan name_span;\n"
    "    MinicSourceSpan arguments_span;\n"
    "    bool has_arguments;\n"
    "} MinicParsedAttribute;\n\n"
    "typedef struct MinicParsedAttributeList {\n"
    "    MinicParsedAttribute values[MINIC_MAX_PARSED_ATTRIBUTES];\n"
    "    size_t count;\n"
    "} MinicParsedAttributeList;\n\n",
    "parsed-attribute-list",
)
text = replace_once(
    text,
    "bool minic_parser_parse_gnu_attribute_lists(MinicParser *parser,\n"
    "                                            MinicParsedAttributeConsumer consumer,\n"
    "                                            void *context);\n",
    "bool minic_parser_parse_gnu_attribute_lists(MinicParser *parser,\n"
    "                                            MinicParsedAttributeConsumer consumer,\n"
    "                                            void *context);\n"
    "bool minic_parser_collect_gnu_attribute_lists(MinicParser *parser,\n"
    "                                              MinicParsedAttributeList *attributes);\n",
    "attribute-collector-prototype",
)
text = replace_once(
    text,
    "bool minic_parser_parse_extern_global(MinicParser *parser);\n",
    "bool minic_parser_parse_extern_global(MinicParser *parser);\n"
    "bool minic_parser_parse_extern_global_after_head(\n"
    "    MinicParser *parser,\n"
    "    MinicType base_type,\n"
    "    MinicType first_object_type,\n"
    "    MinicSourceSpan first_name_span,\n"
    "    const char *section_name,\n"
    "    size_t section_name_length,\n"
    "    bool has_section,\n"
    "    MinicSymbolVisibility visibility,\n"
    "    bool has_visibility);\n",
    "extern-after-head-prototype",
)
path.write_text(text)

# 2) Attribute syntax: collect now, validate after declarator kind is known.
path = root / "src/frontend/parser_attribute.c"
text = path.read_text()
collector = r'''

static bool collect_parsed_attribute(MinicParser *parser,
                                     const MinicParsedAttribute *attribute,
                                     void *opaque_context) {
    MinicParsedAttributeList *attributes;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    attributes = (MinicParsedAttributeList *)opaque_context;
    if (attributes->count >= MINIC_MAX_PARSED_ATTRIBUTES) {
        minic_parser_error(parser, "too many GNU attributes on one declaration");
        return false;
    }
    attributes->values[attributes->count] = *attribute;
    attributes->count += 1U;
    return true;
}

bool minic_parser_collect_gnu_attribute_lists(MinicParser *parser,
                                              MinicParsedAttributeList *attributes) {
    if (parser == NULL || attributes == NULL) {
        return false;
    }
    return minic_parser_parse_gnu_attribute_lists(parser, collect_parsed_attribute, attributes);
}
'''
if "bool minic_parser_collect_gnu_attribute_lists" not in text:
    text = text.rstrip() + collector + "\n"
path.write_text(text)

# 3) Extern object parser: split already-parsed first declarator from the remaining tail.
path = root / "src/frontend/parser_global.c"
text = path.read_text()
new_extern = r'''static bool parse_extern_object_declarator(MinicParser *parser,
                                           MinicType base_type,
                                           MinicSourceSpan *name_span,
                                           MinicType *object_type) {
    if (parser == NULL || name_span == NULL || object_type == NULL ||
        !minic_parser_parse_pointer_declarator(parser, base_type, object_type)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        return parse_extern_function_pointer_object_declarator(
            parser, *object_type, name_span, object_type);
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected extern object name");
        return false;
    }
    *name_span = parser->current.span;
    return minic_parser_advance(parser);
}

bool minic_parser_parse_extern_global_after_head(
    MinicParser *parser,
    MinicType base_type,
    MinicType first_object_type,
    MinicSourceSpan first_name_span,
    const char *section_name,
    size_t section_name_length,
    bool has_section,
    MinicSymbolVisibility visibility,
    bool has_visibility) {
    bool first_declarator;

    if (parser == NULL) {
        return false;
    }
    first_declarator = true;
    for (;;) {
        MinicGlobalObjectId object_id;
        MinicSourceSpan name_span;
        MinicType object_type;
        bool is_array;

        if (first_declarator) {
            name_span = first_name_span;
            object_type = first_object_type;
            first_declarator = false;
        } else if (!parse_extern_object_declarator(parser, base_type, &name_span, &object_type)) {
            return false;
        }
        if (minic_type_is_void(object_type) || minic_type_is_function(object_type) ||
            minic_type_is_array(object_type)) {
            minic_parser_error(parser, "unsupported extern object type");
            return false;
        }
        if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
            minic_parser_error(parser, "duplicate global object");
            return false;
        }

        is_array = false;
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t element_count;
            MinicType array_type;

            is_array = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
                if (!minic_c0_program_add_incomplete_array_type(
                        parser->program, object_type, &array_type) ||
                    !minic_parser_advance(parser)) {
                    minic_parser_error(parser, "cannot declare incomplete extern array");
                    return false;
                }
            } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count) ||
                       !minic_c0_program_add_array_type(
                           parser->program, object_type, element_count, &array_type)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot declare extern array");
                }
                return false;
            }
            object_type = array_type;
        }

        if (!minic_c0_program_add_global_object(
                parser->program,
                parser->source + name_span.begin.offset,
                minic_parser_span_length(name_span),
                object_type,
                false,
                is_array ? minic_type_is_const(
                               parser->program->array_types[object_type.array_type_id].element_type)
                         : minic_type_is_const(object_type),
                &object_id) ||
            !minic_c0_global_object_set_extern(parser->program, object_id) ||
            (has_section && !minic_c0_global_object_set_section(
                                parser->program, object_id, section_name, section_name_length)) ||
            (has_visibility &&
             !minic_c0_global_object_set_visibility(parser->program, object_id, visibility))) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot declare extern object");
            }
            return false;
        }

        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }

    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after extern object declaration");
}

bool minic_parser_parse_extern_global(MinicParser *parser) {
    MinicSourceSpan first_name_span;
    MinicType base_type;
    MinicType first_object_type;
    char section_name[256];
    size_t section_name_length;
    bool has_section;

    section_name_length = 0U;
    has_section = false;
    (void)memset(section_name, 0, sizeof(section_name));
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_section_attribute(
            parser, section_name, sizeof(section_name), &section_name_length, &has_section) ||
        !parse_extern_object_declarator(
            parser, base_type, &first_name_span, &first_object_type)) {
        return false;
    }
    return minic_parser_parse_extern_global_after_head(parser,
                                                       base_type,
                                                       first_object_type,
                                                       first_name_span,
                                                       section_name,
                                                       section_name_length,
                                                       has_section,
                                                       MINIC_SYMBOL_VISIBILITY_DEFAULT,
                                                       false);
}

'''
text = replace_range(
    text,
    "bool minic_parser_parse_extern_global(MinicParser *parser) {",
    "static bool\nparse_static_pointer_array",
    new_extern,
    "extern-global-split",
)
path.write_text(text)

# 4) Function/top-level path: collect prefix attributes, classify on the real parse, remove extern probe.
path = root / "src/frontend/parser_function.c"
text = path.read_text()
insert_after = '''static bool parse_function_attribute_lists(MinicParser *parser,\n                                           bool allow_gnu_inline,\n                                           bool is_internal,\n                                           bool is_inline,\n                                           const char *unsupported_message) {\n    MinicFunctionAttributeContext context;\n\n    context.allow_gnu_inline = allow_gnu_inline;\n    context.is_internal = is_internal;\n    context.is_inline = is_inline;\n    context.unsupported_message = unsupported_message;\n    return minic_parser_parse_gnu_attribute_lists(parser, consume_function_attribute, &context);\n}\n'''
extra_helpers = r'''

static bool apply_function_attribute_list(MinicParser *parser,
                                          const MinicParsedAttributeList *attributes,
                                          bool allow_gnu_inline,
                                          bool is_internal,
                                          bool is_inline,
                                          const char *unsupported_message) {
    MinicFunctionAttributeContext context;
    size_t index;

    if (parser == NULL || attributes == NULL) {
        return false;
    }
    context.allow_gnu_inline = allow_gnu_inline;
    context.is_internal = is_internal;
    context.is_inline = is_inline;
    context.unsupported_message = unsupported_message;
    for (index = 0U; index < attributes->count; ++index) {
        if (!consume_function_attribute(parser, &attributes->values[index], &context)) {
            return false;
        }
    }
    return true;
}

static bool validate_external_object_attribute_list(
    MinicParser *parser, const MinicParsedAttributeList *attributes) {
    size_t index;

    if (parser == NULL || attributes == NULL) {
        return false;
    }
    for (index = 0U; index < attributes->count; ++index) {
        const MinicAttributeDescriptor *descriptor = attributes->values[index].descriptor;

        if (descriptor == NULL ||
            !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_OBJECT) ||
            !function_attribute_class_is_parse_only(descriptor->semantic_class)) {
            minic_parser_error(
                parser,
                "unsupported GNU external-object prefix attribute; symbol/layout attributes require "
                "explicit object semantics");
            return false;
        }
    }
    return true;
}
'''
text = replace_once(text, insert_after, insert_after + extra_helpers, "deferred-attribute-helpers")
text = replace_once(
    text,
    '''static bool parse_gnu_predeclarator_function_attributes(MinicParser *parser) {\n    return minic_parser_parse_gnu_function_attributes(parser);\n}\n\n''',
    "",
    "remove-predeclarator-wrapper",
)
text = replace_once(
    text,
    "    MinicType return_type;\n",
    "    MinicType base_type;\n    MinicType return_type;\n    MinicParsedAttributeList deferred_attributes;\n",
    "function-head-types",
)
text = replace_once(
    text,
    "    bool is_inline;\n",
    "    bool is_function_pointer_object;\n    bool is_inline;\n",
    "function-head-flags",
)
text = replace_once(
    text,
    "    body_block = MINIC_BLOCK_INVALID;\n    parameter_count = 0U;\n    is_inline = false;\n",
    "    body_block = MINIC_BLOCK_INVALID;\n    parameter_count = 0U;\n    is_function_pointer_object = false;\n    is_inline = false;\n    (void)memset(&deferred_attributes, 0, sizeof(deferred_attributes));\n",
    "function-head-init",
)
old_head = r'''    if (!minic_parser_parse_gnu_prefix_function_attributes(parser, is_internal, is_inline)) {
        return false;
    }
    if (!minic_parser_parse_type_name(parser, &return_type)) {
        return false;
    }
    if (!minic_parser_parse_gnu_section_attribute(
            parser, section_name, sizeof(section_name), &section_name_length, &has_section) ||
        !parse_gnu_predeclarator_function_attributes(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected function name in parenthesized declarator");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected ')' after parenthesized function name")) {
            return false;
        }
    } else {
        minic_parser_error(parser, "expected function name");
        return false;
    }
'''
new_head = r'''    if (!minic_parser_collect_gnu_attribute_lists(parser, &deferred_attributes) ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_section_attribute(
            parser, section_name, sizeof(section_name), &section_name_length, &has_section) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, &return_type) ||
        !minic_parser_parse_gnu_section_attribute(
            parser, section_name, sizeof(section_name), &section_name_length, &has_section) ||
        !minic_parser_collect_gnu_attribute_lists(parser, &deferred_attributes)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicParser name_probe = *parser;

        if (!minic_parser_advance(&name_probe)) {
            return false;
        }
        if (name_probe.current.kind == MINIC_TOKEN_STAR) {
            MinicParsedFunctionDeclarator declarator;

            if (!minic_parser_parse_parenthesized_function_declarator(
                    parser, true, true, &declarator)) {
                return false;
            }
            if (declarator.is_variadic) {
                minic_parser_error(
                    parser, "variadic extern function pointer objects are not supported yet");
                return false;
            }
            if (!minic_parser_build_function_declarator_type(
                    parser, return_type, &declarator, &return_type)) {
                minic_parser_error(parser, "cannot build extern function pointer object type");
                return false;
            }
            name_span = declarator.name_span;
            is_function_pointer_object = true;
        } else {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                minic_parser_error(parser, "expected function name in parenthesized declarator");
                return false;
            }
            name_span = parser->current.span;
            if (!minic_parser_advance(parser) ||
                !minic_parser_expect(
                    parser, MINIC_TOKEN_RPAREN, "expected ')' after parenthesized function name")) {
                return false;
            }
        }
    } else {
        minic_parser_error(parser, "expected function or extern object name");
        return false;
    }
'''
text = replace_once(text, old_head, new_head, "single-pass-head")
old_dispatch = r'''    function_id = minic_parser_find_function(parser, name_span);
    is_main = minic_parser_span_length(name_span) == 4U &&
              memcmp(parser->source + name_span.begin.offset, "main", 4U) == 0;
    if (is_main && !minic_type_is_integer(return_type)) {
        minic_parser_error(parser, "main must return int");
        return false;
    }
    if (is_main && is_internal) {
        minic_parser_error(parser, "main cannot have internal linkage");
        return false;
    }

    if (!is_internal && parser->current.kind != MINIC_TOKEN_LPAREN) {
        if (is_inline) {
            minic_parser_error(parser, "inline specifier requires a function declarator");
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            return parse_visible_external_array(
                parser, return_type, name_span, visibility, has_visibility);
        }
        return parse_external_object_definition(parser, return_type, name_span);
    }
'''
new_dispatch = r'''    if (!is_internal && (is_function_pointer_object || parser->current.kind != MINIC_TOKEN_LPAREN)) {
        if (is_inline) {
            minic_parser_error(parser, "inline specifier requires a function declarator");
            return false;
        }
        if (!validate_external_object_attribute_list(parser, &deferred_attributes)) {
            return false;
        }
        if (is_function_pointer_object || parser->current.kind == MINIC_TOKEN_SEMICOLON ||
            parser->current.kind == MINIC_TOKEN_COMMA) {
            return minic_parser_parse_extern_global_after_head(parser,
                                                               base_type,
                                                               return_type,
                                                               name_span,
                                                               section_name,
                                                               section_name_length,
                                                               has_section,
                                                               visibility,
                                                               has_visibility);
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            return parse_visible_external_array(
                parser, return_type, name_span, visibility, has_visibility);
        }
        return parse_external_object_definition(parser, return_type, name_span);
    }
    if (!apply_function_attribute_list(
            parser,
            &deferred_attributes,
            true,
            is_internal,
            is_inline,
            "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must "
            "be implemented explicitly") ||
        !minic_parser_require_complete_object_type(
            parser, return_type, "incomplete record type requires pointer declarator")) {
        return false;
    }

    function_id = minic_parser_find_function(parser, name_span);
    is_main = minic_parser_span_length(name_span) == 4U &&
              memcmp(parser->source + name_span.begin.offset, "main", 4U) == 0;
    if (is_main && !minic_type_is_integer(return_type)) {
        minic_parser_error(parser, "main must return int");
        return false;
    }
    if (is_main && is_internal) {
        minic_parser_error(parser, "main cannot have internal linkage");
        return false;
    }

'''
text = replace_once(text, old_dispatch, new_dispatch, "extern-function-object-dispatch")
text = replace_range(
    text,
    "static bool extern_declaration_is_function(MinicParser *parser, bool *is_function) {",
    "static bool record_keyword_starts_standalone_declaration",
    "",
    "remove-extern-semantic-probe",
)
old_top = r'''        } else if (parser.current.kind == MINIC_TOKEN_KW_EXTERN) {
            bool is_function;

            if (!extern_declaration_is_function(&parser, &is_function)) {
                success = false;
            } else if (is_function) {
                success = parse_function(&parser, false);
            } else {
                success = minic_parser_parse_extern_global(&parser);
            }
'''
new_top = r'''        } else if (parser.current.kind == MINIC_TOKEN_KW_EXTERN) {
            success = parse_function(&parser, false);
'''
text = replace_once(text, old_top, new_top, "extern-top-level-dispatch")
path.write_text(text)

# 5) Focused regression: exact Linux declaration shape + structural no-probe guard.
fixture = root / "tests/compiler/c0/extern_interleaved_function_attributes.c"
fixture.write_text(r'''extern __attribute__((__format__(printf, 4, 5)))
void warn_slowpath_fmt(const char *file,
                       const int line,
                       unsigned taint,
                       const char *fmt,
                       ...);

extern __attribute__((__format__(printf, 1, 2)))
void warn_printk(const char *fmt, ...);

int declaration_head_probe(int value) {
    warn_slowpath_fmt("probe.c", 7, 0U, "%d", value);
    warn_printk("%d", value);
    return value;
}
''')
runner = root / "tests/compiler/c0/run-extern-interleaved-function-attributes.sh"
runner.write_text(r'''#!/bin/sh
set -eu

: "${MINIC:?MINIC must point at the MiniC executable}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

case "$MINIC" in
    /*) ;;
    *) MINIC="$(pwd)/$MINIC" ;;
esac

work="$BUILD_DIR/tests/compiler-c0-extern-interleaved-function-attributes"
mkdir -p "$work"
"$MINIC" tests/compiler/c0/extern_interleaved_function_attributes.c -S -o "$work/probe.s"

grep -q 'warn_slowpath_fmt' "$work/probe.s"
grep -q 'warn_printk' "$work/probe.s"
if grep -q 'extern_declaration_is_function' src/frontend/parser_function.c; then
    echo 'FAIL compiler/c0/extern_interleaved_function_attributes semantic-probe-still-present=1' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/extern_interleaved_function_attributes linux-shape=1 single-pass-extern-head=1 deferred-prefix-attributes=1'
''')

path = root / "tools/dev/pr76-focused.sh"
text = path.read_text()
text = replace_once(
    text,
    "sh tests/compiler/c0/run-extern-function-pointer-object.sh\n",
    "sh tests/compiler/c0/run-extern-function-pointer-object.sh\n"
    "sh tests/compiler/c0/run-extern-interleaved-function-attributes.sh\n",
    "focused-extern-head-gate",
)
path.write_text(text)
