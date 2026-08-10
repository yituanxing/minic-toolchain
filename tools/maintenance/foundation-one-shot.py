#!/usr/bin/env python3
"""Temporary exact-anchor migration for the Foundation consolidation line."""

from pathlib import Path


def replace_one(path: str, old: str, new: str, label: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    file.write_text(text.replace(old, new, 1))


def migrate_attributes() -> None:
    header = Path("src/frontend/attribute.h")
    if "MINIC_ATTRIBUTE_ALIGNED" in header.read_text():
        print("attribute registry already migrated")
        return

    replace_one(
        "src/frontend/attribute.h",
        "    MINIC_ATTRIBUTE_GNU_INLINE,\n    MINIC_ATTRIBUTE_SECTION,\n    MINIC_ATTRIBUTE_VISIBILITY\n} MinicAttributeKind;",
        "    MINIC_ATTRIBUTE_GNU_INLINE,\n    MINIC_ATTRIBUTE_SECTION,\n    MINIC_ATTRIBUTE_VISIBILITY,\n    MINIC_ATTRIBUTE_PACKED,\n    MINIC_ATTRIBUTE_ALIGNED\n} MinicAttributeKind;",
        "attribute kinds",
    )
    replace_one(
        "src/frontend/attribute.h",
        "    MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW,\n    MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,\n    MINIC_ATTRIBUTE_CLASS_SYMBOL\n} MinicAttributeClass;",
        "    MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW,\n    MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,\n    MINIC_ATTRIBUTE_CLASS_SYMBOL,\n    MINIC_ATTRIBUTE_CLASS_LAYOUT\n} MinicAttributeClass;",
        "attribute classes",
    )
    replace_one(
        "src/frontend/attribute.c",
        '    MINIC_ATTRIBUTE_ENTRY("__visibility__",\n                          MINIC_ATTRIBUTE_VISIBILITY,\n                          MINIC_ATTRIBUTE_CLASS_SYMBOL,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),\n};',
        '    MINIC_ATTRIBUTE_ENTRY("__visibility__",\n                          MINIC_ATTRIBUTE_VISIBILITY,\n                          MINIC_ATTRIBUTE_CLASS_SYMBOL,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),\n    MINIC_ATTRIBUTE_ENTRY("packed",\n                          MINIC_ATTRIBUTE_PACKED,\n                          MINIC_ATTRIBUTE_CLASS_LAYOUT,\n                          MINIC_ATTRIBUTE_TARGET_TYPE),\n    MINIC_ATTRIBUTE_ENTRY("__packed__",\n                          MINIC_ATTRIBUTE_PACKED,\n                          MINIC_ATTRIBUTE_CLASS_LAYOUT,\n                          MINIC_ATTRIBUTE_TARGET_TYPE),\n    MINIC_ATTRIBUTE_ENTRY("aligned",\n                          MINIC_ATTRIBUTE_ALIGNED,\n                          MINIC_ATTRIBUTE_CLASS_LAYOUT,\n                          MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),\n    MINIC_ATTRIBUTE_ENTRY("__aligned__",\n                          MINIC_ATTRIBUTE_ALIGNED,\n                          MINIC_ATTRIBUTE_CLASS_LAYOUT,\n                          MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),\n};',
        "layout attribute descriptors",
    )

    replace_one(
        "src/frontend/parser_internal.h",
        '#include "frontend/ast.h"\n',
        '#include "frontend/ast.h"\n#include "frontend/attribute.h"\n',
        "parser attribute include",
    )
    replace_one(
        "src/frontend/parser_internal.h",
        "bool minic_parser_span_equals(const MinicParser *parser,\n                              MinicSourceSpan left,\n                              MinicSourceSpan right);\n",
        "bool minic_parser_span_equals(const MinicParser *parser,\n                              MinicSourceSpan left,\n                              MinicSourceSpan right);\nconst MinicAttributeDescriptor *minic_parser_current_attribute(const MinicParser *parser);\nbool minic_parser_current_attribute_is(const MinicParser *parser,\n                                       MinicAttributeKind kind,\n                                       MinicAttributeTarget target);\n",
        "parser attribute declarations",
    )
    replace_one(
        "src/frontend/parser_core.c",
        "bool minic_parser_span_equals(const MinicParser *parser,\n                              MinicSourceSpan left,\n                              MinicSourceSpan right) {\n    size_t left_length;\n    size_t right_length;\n\n    left_length = minic_parser_span_length(left);\n    right_length = minic_parser_span_length(right);\n    return left_length == right_length && memcmp(parser->source + left.begin.offset,\n                                                 parser->source + right.begin.offset,\n                                                 left_length) == 0;\n}\n\n",
        "bool minic_parser_span_equals(const MinicParser *parser,\n                              MinicSourceSpan left,\n                              MinicSourceSpan right) {\n    size_t left_length;\n    size_t right_length;\n\n    left_length = minic_parser_span_length(left);\n    right_length = minic_parser_span_length(right);\n    return left_length == right_length && memcmp(parser->source + left.begin.offset,\n                                                 parser->source + right.begin.offset,\n                                                 left_length) == 0;\n}\n\nconst MinicAttributeDescriptor *minic_parser_current_attribute(const MinicParser *parser) {\n    size_t name_length;\n\n    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {\n        return NULL;\n    }\n    name_length = minic_parser_span_length(parser->current.span);\n    return minic_attribute_lookup(parser->source + parser->current.span.begin.offset, name_length);\n}\n\nbool minic_parser_current_attribute_is(const MinicParser *parser,\n                                       MinicAttributeKind kind,\n                                       MinicAttributeTarget target) {\n    const MinicAttributeDescriptor *descriptor;\n\n    descriptor = minic_parser_current_attribute(parser);\n    return descriptor != NULL && descriptor->kind == kind &&\n           minic_attribute_allowed_on(descriptor, target);\n}\n\n",
        "parser attribute helpers",
    )

    function = Path("src/frontend/parser_function.c")
    text = function.read_text()
    helper = "static const MinicAttributeDescriptor *current_function_attribute(const MinicParser *parser) {\n    size_t name_length;\n\n    if (parser == NULL || parser->current.kind == MINIC_TOKEN_EOF) {\n        return NULL;\n    }\n    name_length = minic_parser_span_length(parser->current.span);\n    return minic_attribute_lookup(parser->source + parser->current.span.begin.offset, name_length);\n}\n\n"
    if text.count(helper) != 1:
        raise SystemExit("function attribute helper: anchor mismatch")
    text = text.replace(helper, "", 1)
    text = text.replace(
        "descriptor = current_function_attribute(parser);",
        "descriptor = minic_parser_current_attribute(parser);",
    )
    text = text.replace(
        'minic_parser_error(parser, "at most eight parameters are supported");',
        'minic_parser_error(parser, "parameter count exceeds compiler limit");',
    )
    if "current_function_attribute(" in text:
        raise SystemExit("function attribute helper still referenced")
    function.write_text(text)

    record = Path("src/frontend/parser_record.c")
    text = record.read_text()
    pairs = [
        (
            '    if (!token_text_equals(parser, parser->current, "__packed__") &&\n        !token_text_equals(parser, parser->current, "packed")) {',
            '    if (!minic_parser_current_attribute_is(\n            parser, MINIC_ATTRIBUTE_PACKED, MINIC_ATTRIBUTE_TARGET_TYPE)) {',
            "packed record",
        ),
        (
            '    if (!token_text_equals(parser, parser->current, "__aligned__") &&\n        !token_text_equals(parser, parser->current, "aligned")) {',
            '    if (!minic_parser_current_attribute_is(\n            parser, MINIC_ATTRIBUTE_ALIGNED, MINIC_ATTRIBUTE_TARGET_FIELD)) {',
            "aligned field",
        ),
        (
            '    if (!token_text_equals(parser, parser->current, "aligned") &&\n        !token_text_equals(parser, parser->current, "__aligned__")) {',
            '    if (!minic_parser_current_attribute_is(\n            parser, MINIC_ATTRIBUTE_ALIGNED, MINIC_ATTRIBUTE_TARGET_TYPE)) {',
            "aligned record",
        ),
    ]
    for old, new, label in pairs:
        if text.count(old) != 1:
            raise SystemExit(f"{label}: anchor mismatch")
        text = text.replace(old, new, 1)
    record.write_text(text)

    replace_one(
        "src/frontend/parser_typedef.c",
        '    if (!typedef_token_text_equals(parser, "aligned") &&\n        !typedef_token_text_equals(parser, "__aligned__")) {',
        '    if (!minic_parser_current_attribute_is(\n            parser, MINIC_ATTRIBUTE_ALIGNED, MINIC_ATTRIBUTE_TARGET_TYPE)) {',
        "typedef aligned",
    )

    replace_one(
        "src/frontend/parser_type.c",
        "\n#include <string.h>\n\n#include <string.h>\n\nstatic bool minic_parser_identifier_is",
        "\nstatic bool minic_parser_identifier_is",
        "parser_type duplicate include",
    )
    replace_one(
        "src/frontend/parser_global.c",
        "#include <limits.h>\n#include <limits.h>\n",
        "#include <limits.h>\n",
        "parser_global duplicate include",
    )


def repair_materialized_contracts() -> None:
    replace_one(
        "tests/frontend/type_test.c",
        "        !minic_type_cast_compatible(unsigned_char_type, integer_type) ||\n        !minic_type_cast_compatible(integer_type, unsigned_char_type) ||\n        minic_type_cast_compatible(void_pointer_type, integer_type)) {",
        "        !minic_type_cast_compatible(unsigned_char_type, integer_type) ||\n        !minic_type_cast_compatible(integer_type, unsigned_char_type) ||\n        !minic_type_cast_compatible(void_pointer_type, integer_type) ||\n        !minic_type_cast_compatible(integer_type, void_pointer_type)) {",
        "pointer/integer cast unit contract",
    )

    verifier = Path("src/frontend/ast_verifier.c")
    text = verifier.read_text()
    replacements = [
        ("        const MinicExpression *left;\n        const MinicExpression *right;\n        const MinicExpression *result_pointer;", "        const MinicExpression *overflow_left;\n        const MinicExpression *overflow_right;\n        const MinicExpression *result_pointer;"),
        ("        left = expression_before(program, expression->value.overflow.left, expression_index);", "        overflow_left =\n            expression_before(program, expression->value.overflow.left, expression_index);"),
        ("        right = expression_before(program, expression->value.overflow.right, expression_index);", "        overflow_right =\n            expression_before(program, expression->value.overflow.right, expression_index);"),
        ("        return left != NULL && right != NULL && result_pointer != NULL &&", "        return overflow_left != NULL && overflow_right != NULL && result_pointer != NULL &&"),
        ("               minic_type_equal(left->type, result_type) &&", "               minic_type_equal(overflow_left->type, result_type) &&"),
        ("               minic_type_equal(right->type, result_type);", "               minic_type_equal(overflow_right->type, result_type);"),
    ]
    for old, new in replacements:
        if text.count(old) != 1:
            raise SystemExit(f"overflow verifier anchor mismatch: {old!r}")
        text = text.replace(old, new, 1)
    verifier.write_text(text)


def repair_final_runner() -> None:
    path = Path("tests/compiler/c0/run.sh")
    text = path.read_text()

    old = 'expect_compile_failure \\\n    invalid_assignment_rvalue \\\n    "assignment expression requires a modifiable scalar lvalue"'
    new = 'expect_compile_failure \\\n    invalid_assignment_rvalue \\\n    "assignment expression requires a modifiable object lvalue"'
    if text.count(old) != 1:
        raise SystemExit(f"assignment-rvalue final anchor count={text.count(old)}")
    text = text.replace(old, new, 1)

    old = 'expect_compile_failure \\\n    invalid_empty_record \\\n    "record definition requires at least one field"'
    new = 'MINIC="$minic" \\\nHOST_CC="$host_cc" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-gnu-empty-records.sh"'
    if text.count(old) != 1:
        raise SystemExit(f"empty-record final anchor count={text.count(old)}")
    text = text.replace(old, new, 1)

    old = '    grep -F "$expected_message" "$work/$name.stderr" >/dev/null\n    printf \'%s\\n\' "PASS compiler/c0/$name"\n'
    new = '    if ! grep -F "$expected_message" "$work/$name.stderr" >/dev/null; then\n        printf \'%s\\n\' "FAIL compiler/c0/$name: diagnostic mismatch" >&2\n        cat "$work/$name.stderr" >&2\n        exit 1\n    fi\n    printf \'%s\\n\' "PASS compiler/c0/$name"\n'
    if text.count(old) != 1:
        raise SystemExit(f"diagnostic helper final anchor count={text.count(old)}")
    text = text.replace(old, new, 1)
    path.write_text(text)


def main() -> None:
    migrate_attributes()
    repair_materialized_contracts()
    repair_final_runner()
    print("foundation one-shot migration applied")


if __name__ == "__main__":
    main()
