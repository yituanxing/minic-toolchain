#!/usr/bin/env python3
"""One-shot migration helper for the foundation consolidation branch.

This file is intentionally temporary.  It applies only exact, audited anchors and
must be deleted after the resulting source/test commit passes the full PR gates.
"""

from pathlib import Path


def replace_one(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1))


def migrate_attribute_registry() -> None:
    attribute_h = Path("src/frontend/attribute.h")
    if "MINIC_ATTRIBUTE_ALIGNED" in attribute_h.read_text():
        print("attribute registry coverage already migrated")
        return

    replace_one(
        attribute_h,
        """    MINIC_ATTRIBUTE_GNU_INLINE,
    MINIC_ATTRIBUTE_SECTION,
    MINIC_ATTRIBUTE_VISIBILITY
} MinicAttributeKind;""",
        """    MINIC_ATTRIBUTE_GNU_INLINE,
    MINIC_ATTRIBUTE_SECTION,
    MINIC_ATTRIBUTE_VISIBILITY,
    MINIC_ATTRIBUTE_PACKED,
    MINIC_ATTRIBUTE_ALIGNED
} MinicAttributeKind;""",
        "attribute kinds",
    )
    replace_one(
        attribute_h,
        """    MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW,
    MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,
    MINIC_ATTRIBUTE_CLASS_SYMBOL
} MinicAttributeClass;""",
        """    MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW,
    MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,
    MINIC_ATTRIBUTE_CLASS_SYMBOL,
    MINIC_ATTRIBUTE_CLASS_LAYOUT
} MinicAttributeClass;""",
        "attribute classes",
    )

    replace_one(
        Path("src/frontend/attribute.c"),
        """    MINIC_ATTRIBUTE_ENTRY("__visibility__",
                          MINIC_ATTRIBUTE_VISIBILITY,
                          MINIC_ATTRIBUTE_CLASS_SYMBOL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),
};""",
        """    MINIC_ATTRIBUTE_ENTRY("__visibility__",
                          MINIC_ATTRIBUTE_VISIBILITY,
                          MINIC_ATTRIBUTE_CLASS_SYMBOL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),
    MINIC_ATTRIBUTE_ENTRY("packed",
                          MINIC_ATTRIBUTE_PACKED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_TYPE),
    MINIC_ATTRIBUTE_ENTRY("__packed__",
                          MINIC_ATTRIBUTE_PACKED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_TYPE),
    MINIC_ATTRIBUTE_ENTRY("aligned",
                          MINIC_ATTRIBUTE_ALIGNED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),
    MINIC_ATTRIBUTE_ENTRY("__aligned__",
                          MINIC_ATTRIBUTE_ALIGNED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),
};""",
        "layout attribute descriptors",
    )

    parser_internal = Path("src/frontend/parser_internal.h")
    replace_one(
        parser_internal,
        '#include "frontend/ast.h"\n',
        '#include "frontend/ast.h"\n#include "frontend/attribute.h"\n',
        "parser attribute include",
    )
    replace_one(
        parser_internal,
        """bool minic_parser_span_equals(const MinicParser *parser,
                              MinicSourceSpan left,
                              MinicSourceSpan right);
""",
        """bool minic_parser_span_equals(const MinicParser *parser,
                              MinicSourceSpan left,
                              MinicSourceSpan right);
const MinicAttributeDescriptor *minic_parser_current_attribute(const MinicParser *parser);
bool minic_parser_current_attribute_is(const MinicParser *parser,
                                       MinicAttributeKind kind,
                                       MinicAttributeTarget target);
""",
        "parser attribute helper declarations",
    )

    replace_one(
        Path("src/frontend/parser_core.c"),
        """bool minic_parser_span_equals(const MinicParser *parser,
                              MinicSourceSpan left,
                              MinicSourceSpan right) {
    size_t left_length;
    size_t right_length;

    left_length = minic_parser_span_length(left);
    right_length = minic_parser_span_length(right);
    return left_length == right_length && memcmp(parser->source + left.begin.offset,
                                                 parser->source + right.begin.offset,
                                                 left_length) == 0;
}

""",
        """bool minic_parser_span_equals(const MinicParser *parser,
                              MinicSourceSpan left,
                              MinicSourceSpan right) {
    size_t left_length;
    size_t right_length;

    left_length = minic_parser_span_length(left);
    right_length = minic_parser_span_length(right);
    return left_length == right_length && memcmp(parser->source + left.begin.offset,
                                                 parser->source + right.begin.offset,
                                                 left_length) == 0;
}

const MinicAttributeDescriptor *minic_parser_current_attribute(const MinicParser *parser) {
    size_t name_length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return NULL;
    }
    name_length = minic_parser_span_length(parser->current.span);
    return minic_attribute_lookup(parser->source + parser->current.span.begin.offset, name_length);
}

bool minic_parser_current_attribute_is(const MinicParser *parser,
                                       MinicAttributeKind kind,
                                       MinicAttributeTarget target) {
    const MinicAttributeDescriptor *descriptor;

    descriptor = minic_parser_current_attribute(parser);
    return descriptor != NULL && descriptor->kind == kind &&
           minic_attribute_allowed_on(descriptor, target);
}

""",
        "parser attribute helper implementation",
    )

    path = Path("src/frontend/parser_function.c")
    text = path.read_text()
    old = """static const MinicAttributeDescriptor *current_function_attribute(const MinicParser *parser) {
    size_t name_length;

    if (parser == NULL || parser->current.kind == MINIC_TOKEN_EOF) {
        return NULL;
    }
    name_length = minic_parser_span_length(parser->current.span);
    return minic_attribute_lookup(parser->source + parser->current.span.begin.offset, name_length);
}

"""
    if text.count(old) != 1:
        raise SystemExit("function attribute helper: anchor mismatch")
    text = text.replace(old, "", 1)
    text = text.replace(
        "descriptor = current_function_attribute(parser);",
        "descriptor = minic_parser_current_attribute(parser);",
    )
    if "current_function_attribute(" in text:
        raise SystemExit("function attribute helper still referenced")
    text = text.replace(
        'minic_parser_error(parser, "at most eight parameters are supported");',
        'minic_parser_error(parser, "parameter count exceeds compiler limit");',
    )
    path.write_text(text)

    path = Path("src/frontend/parser_record.c")
    text = path.read_text()
    replacements = [
        (
            """    if (!token_text_equals(parser, parser->current, "__packed__") &&
        !token_text_equals(parser, parser->current, "packed")) {""",
            """    if (!minic_parser_current_attribute_is(
            parser, MINIC_ATTRIBUTE_PACKED, MINIC_ATTRIBUTE_TARGET_TYPE)) {""",
            "packed record classifier",
        ),
        (
            """    if (!token_text_equals(parser, parser->current, "__aligned__") &&
        !token_text_equals(parser, parser->current, "aligned")) {""",
            """    if (!minic_parser_current_attribute_is(
            parser, MINIC_ATTRIBUTE_ALIGNED, MINIC_ATTRIBUTE_TARGET_FIELD)) {""",
            "field aligned classifier",
        ),
        (
            """    if (!token_text_equals(parser, parser->current, "aligned") &&
        !token_text_equals(parser, parser->current, "__aligned__")) {""",
            """    if (!minic_parser_current_attribute_is(
            parser, MINIC_ATTRIBUTE_ALIGNED, MINIC_ATTRIBUTE_TARGET_TYPE)) {""",
            "record aligned classifier",
        ),
    ]
    for old, new, label in replacements:
        count = text.count(old)
        if count != 1:
            raise SystemExit(f"{label}: expected one anchor, found {count}")
        text = text.replace(old, new, 1)
    path.write_text(text)

    replace_one(
        Path("src/frontend/parser_typedef.c"),
        """    if (!typedef_token_text_equals(parser, "aligned") &&
        !typedef_token_text_equals(parser, "__aligned__")) {""",
        """    if (!minic_parser_current_attribute_is(
            parser, MINIC_ATTRIBUTE_ALIGNED, MINIC_ATTRIBUTE_TARGET_TYPE)) {""",
        "typedef aligned classifier",
    )

    path = Path("src/frontend/parser_type.c")
    text = path.read_text()
    duplicate = "\n#include <string.h>\n\n#include <string.h>\n\nstatic bool minic_parser_identifier_is"
    if duplicate not in text:
        raise SystemExit("parser_type duplicate include anchor missing")
    path.write_text(text.replace(duplicate, "\nstatic bool minic_parser_identifier_is", 1))

    path = Path("src/frontend/parser_global.c")
    text = path.read_text()
    duplicate = "#include <limits.h>\n#include <limits.h>\n"
    if duplicate not in text:
        raise SystemExit("parser_global duplicate include anchor missing")
    path.write_text(text.replace(duplicate, "#include <limits.h>\n", 1))


def repair_materialized_contracts() -> None:
    replace_one(
        Path("tests/frontend/type_test.c"),
        """        !minic_type_cast_compatible(unsigned_char_type, integer_type) ||
        !minic_type_cast_compatible(integer_type, unsigned_char_type) ||
        minic_type_cast_compatible(void_pointer_type, integer_type)) {""",
        """        !minic_type_cast_compatible(unsigned_char_type, integer_type) ||
        !minic_type_cast_compatible(integer_type, unsigned_char_type) ||
        !minic_type_cast_compatible(void_pointer_type, integer_type) ||
        !minic_type_cast_compatible(integer_type, void_pointer_type)) {""",
        "pointer/integer cast unit contract",
    )

    path = Path("src/frontend/ast_verifier.c")
    text = path.read_text()
    old = """    case MINIC_EXPRESSION_BUILTIN_OVERFLOW: {
        const MinicExpression *left;
        const MinicExpression *right;
        const MinicExpression *result_pointer;
"""
    new = """    case MINIC_EXPRESSION_BUILTIN_OVERFLOW: {
        const MinicExpression *overflow_left;
        const MinicExpression *overflow_right;
        const MinicExpression *result_pointer;
"""
    if text.count(old) != 1:
        raise SystemExit("overflow verifier declarations: anchor mismatch")
    text = text.replace(old, new, 1)
    replacements = [
        (
            "left = expression_before(program, expression->value.overflow.left, expression_index);",
            "overflow_left =\n            expression_before(program, expression->value.overflow.left, expression_index);",
        ),
        (
            "right = expression_before(program, expression->value.overflow.right, expression_index);",
            "overflow_right =\n            expression_before(program, expression->value.overflow.right, expression_index);",
        ),
        (
            "return left != NULL && right != NULL && result_pointer != NULL &&",
            "return overflow_left != NULL && overflow_right != NULL && result_pointer != NULL &&",
        ),
        (
            "minic_type_equal(left->type, result_type) &&",
            "minic_type_equal(overflow_left->type, result_type) &&",
        ),
        (
            "minic_type_equal(right->type, result_type);",
            "minic_type_equal(overflow_right->type, result_type);",
        ),
    ]
    for old_text, new_text in replacements:
        count = text.count(old_text)
        if count != 1:
            raise SystemExit(f"overflow verifier anchor mismatch: {old_text!r} count={count}")
        text = text.replace(old_text, new_text, 1)
    path.write_text(text)


def repair_final_c0_runner() -> None:
    path = Path("tests/compiler/c0/run.sh")
    replace_one(
        path,
        """expect_compile_failure \\
    invalid_assignment_rvalue \\
    "assignment expression requires a modifiable scalar lvalue"""",
        """expect_compile_failure \\
    invalid_assignment_rvalue \\
    "assignment expression requires a modifiable object lvalue"""",
        "final assignment-rvalue diagnostic",
    )
    replace_one(
        path,
        """expect_compile_failure \\
    invalid_empty_record \\
    "record definition requires at least one field"""",
        """MINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-gnu-empty-records.sh"""",
        "GNU empty-record final gate",
    )
    replace_one(
        path,
        '''    grep -F "$expected_message" "$work/$name.stderr" >/dev/null
    printf '%s\\n' "PASS compiler/c0/$name"
''',
        '''    if ! grep -F "$expected_message" "$work/$name.stderr" >/dev/null; then
        printf '%s\\n' "FAIL compiler/c0/$name: diagnostic mismatch" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
    printf '%s\\n' "PASS compiler/c0/$name"
''',
        "final C0 diagnostic visibility",
    )


def main() -> None:
    migrate_attribute_registry()
    repair_materialized_contracts()
    repair_final_c0_runner()
    print("foundation one-shot migration applied")


if __name__ == "__main__":
    main()
