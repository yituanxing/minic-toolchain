#!/usr/bin/env python3
"""One-shot exact-marker migration for shared parenthesized function declarators."""

from pathlib import Path


def replace_unique_range(text: str,
                         start_marker: str,
                         end_marker: str,
                         replacement: str,
                         label: str) -> str:
    start = text.find(start_marker)
    end = text.find(end_marker, start + 1)
    if start < 0 or end < 0:
        raise SystemExit(f"{label}: migration markers missing")
    if text.find(start_marker, start + 1) >= 0:
        raise SystemExit(f"{label}: start marker is not unique")
    return text[:start] + replacement + text[end:]


def update_makefile() -> None:
    path = Path("Makefile")
    text = path.read_text()
    source = "\tsrc/frontend/parser_declarator.c \\\n"
    if source in text:
        return
    anchor = "\tsrc/frontend/parser_core.c \\\n"
    if text.count(anchor) != 1:
        raise SystemExit("Makefile declarator source anchor mismatch")
    path.write_text(text.replace(anchor, source + anchor, 1))


def update_contract() -> None:
    path = Path("src/frontend/parser_internal.h")
    text = path.read_text()
    if "typedef struct MinicParsedFunctionDeclarator" not in text:
        anchor = """typedef bool (*MinicParsedAttributeConsumer)(MinicParser *parser,
                                             const MinicParsedAttribute *attribute,
                                             void *context);

"""
        addition = anchor + """typedef struct MinicParsedFunctionDeclarator {
    MinicSourceSpan name_span;
    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
    size_t parameter_count;
    size_t pointer_depth;
    bool has_name;
    bool is_variadic;
} MinicParsedFunctionDeclarator;

"""
        if text.count(anchor) != 1:
            raise SystemExit("parsed function declarator contract anchor mismatch")
        text = text.replace(anchor, addition, 1)

    if "minic_parser_parse_parenthesized_function_declarator" not in text:
        anchor = """bool minic_parser_parse_gnu_attribute_lists(MinicParser *parser,
                                            MinicParsedAttributeConsumer consumer,
                                            void *context);
"""
        addition = anchor + """bool minic_parser_parse_parenthesized_function_declarator(
    MinicParser *parser,
    bool require_name,
    bool require_pointer,
    MinicParsedFunctionDeclarator *declarator);
bool minic_parser_build_function_declarator_type(
    MinicParser *parser,
    MinicType return_type,
    const MinicParsedFunctionDeclarator *declarator,
    MinicType *declarator_type);
"""
        if text.count(anchor) != 1:
            raise SystemExit("function declarator API anchor mismatch")
        text = text.replace(anchor, addition, 1)
    path.write_text(text)


def migrate_parameter_declarator() -> None:
    path = Path("src/frontend/parser_function.c")
    text = path.read_text()
    replacement = r'''static bool parse_function_pointer_parameter_declarator(MinicParser *parser,
                                                        MinicType return_type,
                                                        MinicSourceSpan *name_span,
                                                        bool *has_name,
                                                        MinicType *parameter_type,
                                                        bool require_name) {
    MinicParsedFunctionDeclarator declarator;

    if (parser == NULL || name_span == NULL || has_name == NULL || parameter_type == NULL ||
        !minic_parser_parse_parenthesized_function_declarator(
            parser, require_name, true, &declarator)) {
        return false;
    }
    if (declarator.is_variadic) {
        minic_parser_error(parser, "variadic function pointer parameters are not supported yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, parameter_type)) {
        minic_parser_error(parser, "cannot build function pointer parameter type");
        return false;
    }
    *name_span = declarator.name_span;
    *has_name = declarator.has_name;
    return true;
}

'''
    text = replace_unique_range(
        text,
        "static bool parse_function_pointer_parameter_declarator(MinicParser *parser,",
        "bool minic_parser_parse_parameter_list(MinicParser *parser,",
        replacement,
        "function pointer parameter declarator",
    )
    path.write_text(text)


def migrate_record_declarator() -> None:
    path = Path("src/frontend/parser_record.c")
    text = path.read_text()
    replacement = r'''static bool parse_function_pointer_field_declarator(MinicParser *parser,
                                                    MinicType return_type,
                                                    MinicSourceSpan *name_span,
                                                    MinicType *field_type) {
    MinicParsedFunctionDeclarator declarator;

    if (parser == NULL || name_span == NULL || field_type == NULL ||
        !minic_parser_parse_parenthesized_function_declarator(parser, true, true, &declarator)) {
        return false;
    }
    if (declarator.is_variadic) {
        minic_parser_error(parser, "variadic function pointer fields are not supported yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, field_type)) {
        minic_parser_error(parser, "cannot build function pointer field type");
        return false;
    }
    *name_span = declarator.name_span;
    return true;
}

'''
    text = replace_unique_range(
        text,
        "static bool parse_function_pointer_field_declarator(MinicParser *parser,",
        "static bool token_text_equals(const MinicParser *parser, MinicToken token, const char *text) {",
        replacement,
        "function pointer field declarator",
    )
    path.write_text(text)


def migrate_typedef_declarator() -> None:
    path = Path("src/frontend/parser_typedef.c")
    text = path.read_text()
    replacement = r'''static bool parse_function_pointer_typedef(MinicParser *parser,
                                           MinicType return_type,
                                           MinicSourceSpan *name_span,
                                           MinicType *aliased_type) {
    MinicParsedFunctionDeclarator declarator;

    if (parser == NULL || name_span == NULL || aliased_type == NULL ||
        !minic_parser_parse_parenthesized_function_declarator(parser, true, false, &declarator)) {
        return false;
    }
    if (declarator.is_variadic) {
        minic_parser_error(parser, "variadic function pointer typedefs are not supported yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, aliased_type)) {
        minic_parser_error(parser, "cannot build function pointer typedef type");
        return false;
    }
    *name_span = declarator.name_span;
    return true;
}

'''
    text = replace_unique_range(
        text,
        "static bool parse_function_pointer_typedef(MinicParser *parser,",
        "static bool typedef_token_text_equals(const MinicParser *parser, const char *text) {",
        replacement,
        "function pointer typedef declarator",
    )
    path.write_text(text)


def migrate_extern_object_declarator() -> None:
    path = Path("src/frontend/parser_global.c")
    text = path.read_text()
    replacement = r'''static bool parse_extern_function_pointer_object_declarator(MinicParser *parser,
                                                            MinicType return_type,
                                                            MinicSourceSpan *name_span,
                                                            MinicType *object_type) {
    MinicParsedFunctionDeclarator declarator;

    if (parser == NULL || name_span == NULL || object_type == NULL ||
        !minic_parser_parse_parenthesized_function_declarator(parser, true, true, &declarator)) {
        return false;
    }
    if (declarator.is_variadic) {
        minic_parser_error(parser,
                           "variadic extern function pointer objects are not supported yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, object_type)) {
        minic_parser_error(parser, "cannot build extern function pointer object type");
        return false;
    }
    *name_span = declarator.name_span;
    return true;
}

'''
    text = replace_unique_range(
        text,
        "static bool parse_extern_function_pointer_object_declarator(MinicParser *parser,",
        "bool minic_parser_parse_extern_global(MinicParser *parser) {",
        replacement,
        "extern function pointer object declarator",
    )
    path.write_text(text)


def wire_focused_gate() -> None:
    path = Path("tools/dev/pr76-focused.sh")
    text = path.read_text()
    gate = "sh tests/compiler/c0/run-shared-function-declarator.sh\n"
    if gate in text:
        return
    anchor = "sh tests/compiler/c0/run-extern-function-pointer-object.sh\n"
    if text.count(anchor) != 1:
        raise SystemExit("shared declarator focused gate anchor mismatch")
    path.write_text(text.replace(anchor, anchor + gate, 1))


def main() -> None:
    update_makefile()
    update_contract()
    migrate_parameter_declarator()
    migrate_record_declarator()
    migrate_typedef_declarator()
    migrate_extern_object_declarator()
    wire_focused_gate()
    print("shared parenthesized function declarator migration applied")


if __name__ == "__main__":
    main()
